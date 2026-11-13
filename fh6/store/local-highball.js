// Optional MACxus adapter for an explicitly selected, existing Highball runtime.
// No CrossOver detection, ownership, Store entitlement or game API is emulated.
import { accessSync, constants, readFileSync, statSync } from 'node:fs';
import { createHash } from 'node:crypto';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

let cached;
export function readLocalHighball(dataRoot) {
  const file = path.join(dataRoot, 'local-highball.json');
  let raw;
  try { raw = readFileSync(file, 'utf8'); }
  catch (e) { if (e.code === 'ENOENT') return undefined; throw e; }
  const config = JSON.parse(raw);
  if (config.schema !== 1 || config.kind !== 'existing-highball') throw Error('Unknown local Highball configuration');
  for (const key of ['engineRoot', 'rendererRoot', 'libraryRoot', 'shimRoot']) {
    if (typeof config[key] !== 'string' || !path.isAbsolute(config[key])) throw Error('Invalid Highball path: ' + key);
  }
  const required = [
    path.join(config.engineRoot, 'engine/bin/wine'),
    path.join(config.engineRoot, 'engine/bin/wineserver'),
    path.join(config.engineRoot, 'engine/lib/wine/x86_64-windows/kernelbase.dll'),
    path.join(config.rendererRoot, 'wine/x86_64-windows/d3d12.dll'),
    path.join(config.rendererRoot, 'external/libd3dshared.dylib'),
    path.join(config.libraryRoot, 'libgnutls.30.dylib'),
    path.join(config.shimRoot, 'wine/x86_64-windows/d3d12.dll'),
    path.join(config.shimRoot, 'wine/x86_64-windows/apd12.dll'),
    path.join(config.shimRoot, 'wine/x86_64-unix/apd12.so'),
  ];
  const signature = raw + required.map(f => {
    const s = statSync(f); return `${f}:${s.size}:${s.mtimeMs}:${s.ctimeMs}`;
  }).join('\n');
  if (cached?.signature === signature) return cached.config;
  required.slice(0, 2).forEach(f => accessSync(f, constants.X_OK));
  for (const file of required) {
    const expected = config.sha256?.[file];
    if (!/^[a-f0-9]{64}$/.test(expected ?? '') || createHash('sha256').update(readFileSync(file)).digest('hex') !== expected)
      throw Error('Local Highball changed; revalidate before use: ' + file);
  }
  cached = {signature, config};
  return config;
}

export function localBootEnvironment(engineRoot) {
  const c = cached?.config;
  if (!c || c.engineRoot !== engineRoot) return {};
  const frameworks = path.join(engineRoot, 'frameworks');
  return {
    DYLD_FALLBACK_LIBRARY_PATH: [path.join(c.rendererRoot, 'external'), frameworks,
      path.join(frameworks, 'GStreamer.framework/Versions/1.0/lib'), c.libraryRoot, '/usr/local/lib', '/usr/lib'].join(':'),
    DYLD_FALLBACK_FRAMEWORK_PATH: [frameworks, '/Library/Frameworks', '/System/Library/Frameworks'].join(':'),
  };
}

export async function prepareLocalHighball(config, request, bottlesRoot, runtimeDlls, profileEnv) {
  // Use the upstream GDK prefix/bootstrap and cloud-save adapter. Only engine
  // location and existing renderer layout differ. No vendor binary is rewritten.
  const {prepareHighballRunner, highballOverrideDirectory, highballWrapperScript} = await import(pathToFileURL(path.join(request.modulesRoot, 'highball-engine.js')));
  const {writeRunnerScript} = await import(pathToFileURL(path.join(request.modulesRoot, 'crossover.js')));
  request.log(`Existing Highball: ${config.engineRoot}; existing D3DMetal: ${config.rendererRoot}. No CrossOver installation selected.`);
  const runner = await prepareHighballRunner(config.engineRoot, config.rendererRoot, config.libraryRoot,
    bottlesRoot, runtimeDlls, profileEnv, request.log, highballOverrideDirectory(request.dataRoot));
  await writeRunnerScript(runner.command, highballWrapperScript(config.engineRoot, config.rendererRoot,
    config.libraryRoot, runner.compatibilityDataPath, profileEnv, {
      dir: config.shimRoot,
      realDll: path.join(config.shimRoot, 'wine/x86_64-windows/apd12.dll'),
    }));
  return runner;
}
