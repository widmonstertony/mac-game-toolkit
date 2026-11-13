// Independent CLI adapter; the installed MACxus app and its signature stay intact.
import path from 'node:path';
import os from 'node:os';
import {fileURLToPath} from 'node:url';
import {readFileSync, existsSync} from 'node:fs';
import {spawn, spawnSync} from 'node:child_process';
import {readLocalHighball, localBootEnvironment, prepareLocalHighball} from './local-highball.js';

const root = path.dirname(fileURLToPath(import.meta.url));
const dataRoot = path.dirname(root);
const mode = process.argv[2] ?? 'setup';
const config = readLocalHighball(dataRoot);
if (!config) throw Error('Run the Store environment setup first');
const modulesRoot = path.join(root, 'upstream/dist-electron/main');
const {bundledRuntimeDllDirectory, runtimeOverrideDirectory} = await import(path.join(modulesRoot,'crossover.js'));
const pointer = JSON.parse(readFileSync(path.join(dataRoot,'runtimes/current.json'),'utf8'));
const runtimeDllDir = path.resolve(path.dirname(pointer.winePath),'../lib/wine/x86_64-windows');
const bundledDir = await bundledRuntimeDllDirectory(runtimeDllDir);
if (!bundledDir) throw Error('Installed Nexus runtime does not match the macOS GDK DLLs');

// All environment changes are confined to this standalone CLI and its children.
Object.assign(process.env, localBootEnvironment(config.engineRoot));
const runner = await prepareLocalHighball(config,{dataRoot, modulesRoot, log: s=>console.error(s)},
  path.join(dataRoot,'bottles'),{dir:runtimeDllDir,overrideDir:runtimeOverrideDirectory(dataRoot),bundledDir},
  {ROSETTA_ADVERTISE_AVX:'0', HB_D3D12_TSSHIM:'1', D3DM_MTL4:'0',
    D3DM_VENDOR_ID:'0x10de',D3DM_DEVICE_ID:'0x2786',D3DM_DEVICE_DESCRIPTION:'NVIDIA GeForce RTX 4070'});

if (mode === 'setup') {
  for (const name of ['concrt140','vcruntime140','vcruntime140_1','msvcp140']) {
    const check=spawnSync(runner.command,['reg','add','HKCU\\Software\\Wine\\DllOverrides','/v',name,
      '/t','REG_SZ','/d','native,builtin','/f'],{encoding:'utf8',env:process.env});
    if (check.status !== 0) throw Error('Could not configure native VC++ runtime: '+name);
  }
  const test = spawnSync(runner.command,['--version'],{encoding:'utf8',env:process.env});
  if (test.status !== 0) throw Error(test.stderr || 'Wine version check failed');
  console.log(JSON.stringify({ready:true,wine:test.stdout.trim(),prefix:runner.compatibilityDataPath,
    command:runner.command,gameDownloaded:false,gameplayVerified:false,appSignatureUnchanged:true},null,2));
} else if (mode === 'run') {
  const source = process.argv[3];
  if (!source || !path.isAbsolute(source)) throw Error('Supply the installed Microsoft Store package directory');
  const manifest = readFileSync(path.join(source,'MicrosoftGame.config'),'utf8');
  if (!/ForzaHorizon6/i.test(manifest)) throw Error('Expected a real Forza Horizon 6 package manifest');
  const {parsePackageXml} = await import(path.join(modulesRoot,'package-xml.js'));
  const tree=parsePackageXml(manifest);
  function executable(node) {
    if (node.name.split(':').at(-1)==='Executable' && /\.exe$/i.test(node.attributes.Name??'')) return node.attributes.Name;
    for (const child of node.children) {const found=executable(child);if(found)return found;}
  }
  const image=executable(tree);
  if (!image || /(^|[\\/])\.\.([\\/]|$)|^[\\/]|:/.test(image)) throw Error('No supported manifest executable');
  // Normal Xodus launch performs Microsoft account/licence validation and its
  // package overlay. Never invoke a game image directly or substitute Steam files.
  const {readBundledBackend}=await import(path.join(modulesRoot,'bundled-backend.js'));
  const {cli}=await readBundledBackend('/Applications/MACxus.app/Contents/Resources/backend');
  const {startXodusService, cloudSaveRuntime}=await import(path.join(modulesRoot,'xodus-service.js'));
  if (!await cloudSaveRuntime(pointer.winePath)) throw Error('Expected the pinned cloud-save runtime');
  const service=await startXodusService(cli,os.tmpdir(),s=>console.error(s));
  let exitCode=1;
  try {
    const environment={...process.env, XODUS_SOCKET:service.socketPath,
      XODUS_BRIDGE_PORT:String(service.bridgePort),XODUS_BRIDGE_KEY:service.bridgeKey,XODUS_SAVE_SYNC:'auto'};
    const proxy=path.resolve(path.dirname(pointer.winePath),'../share/xodus-nexus/compat/PlayFabGameSave.dll');
    if (!existsSync(proxy))throw Error('The PlayFab cloud-save proxy is missing');
    environment.NEXUS_PGS_PROXY=proxy;
    // Keep this event loop running: the game's authenticated TCP bridge is
    // served here. A synchronous child wait would deadlock runtime requests.
    const child=spawn(cli,['run',source,runner.command,'--exe',image,'--materialize-chain'],
      {stdio:'inherit',env:environment,cwd:source});
    const stop=()=>child.kill('SIGINT');process.once('SIGINT',stop);process.once('SIGTERM',stop);
    exitCode=await new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',code=>resolve(code??1));});
    process.removeListener('SIGINT',stop);process.removeListener('SIGTERM',stop);
  } finally {await service.stop();}
  process.exit(exitCode);
} else throw Error('Usage: runner.mjs setup | run /absolute/store/package');
