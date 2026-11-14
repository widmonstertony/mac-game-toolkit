// Independent CLI adapter; the installed MACxus app and its signature stay intact.
import path from 'node:path';
import os from 'node:os';
import {fileURLToPath} from 'node:url';
import {readFileSync, existsSync, closeSync, openSync, mkdtempSync} from 'node:fs';
import {spawn, spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {readLocalHighball, localBootEnvironment, prepareLocalHighball} from './local-highball.js';

const root = path.dirname(fileURLToPath(import.meta.url));
const dataRoot = path.dirname(root);
const mode = process.argv[2] ?? 'setup';
const config = readLocalHighball(dataRoot);
if (!config) throw Error('Run the Store environment setup first');
const profileFile = path.join(root, 'fh6-profile.json');
const profile = existsSync(profileFile) ? JSON.parse(readFileSync(profileFile, 'utf8')) : undefined;
const modulesRoot = path.join(root, 'upstream/dist-electron/main');
const {bundledRuntimeDllDirectory, runtimeOverrideDirectory, MAC_RUNTIME_DLLS} = await import(path.join(modulesRoot,'crossover.js'));
const pointer = JSON.parse(readFileSync(path.join(dataRoot,'runtimes/current.json'),'utf8'));
const runtimeDllDir = path.resolve(path.dirname(pointer.winePath),'../lib/wine/x86_64-windows');
const bundledDir = await bundledRuntimeDllDirectory(runtimeDllDir);
if (!bundledDir) throw Error('Installed Nexus runtime does not match the macOS GDK DLLs');
const repairRoot = path.join(root, 'fh6-nexus-repairs');
const repairManifest = path.join(repairRoot, 'verified.json');
const repairModules = ['winhttp.dll', 'xgameruntime.dll'];
const hashFile = file => createHash('sha256').update(readFileSync(file)).digest('hex');
const runtimeOverrides = runtimeOverrideDirectory(dataRoot);
let nexusRepair;
if (existsSync(repairManifest)) {
  const receipt = JSON.parse(readFileSync(repairManifest, 'utf8'));
  const runtime = path.basename(path.resolve(runtimeDllDir, '../../../..'));
  if (receipt.schema !== 1 || receipt.runtime !== runtime)
    throw Error('The FH6 repair was built for a different Nexus runtime');
  for (const name of MAC_RUNTIME_DLLS) {
    if (existsSync(path.join(runtimeOverrides, name)))
      throw Error('Existing Nexus override conflicts with the FH6 repair: '+name);
  }
  for (const name of repairModules) {
    if (hashFile(path.join(repairRoot, name)) !== receipt.files?.[name])
      throw Error('Updated FH6 repair module does not match the receipt: '+name);
  }
  nexusRepair = repairRoot;
}

// All environment changes are confined to this standalone CLI and its children.
Object.assign(process.env, localBootEnvironment(config.engineRoot));
const runner = await prepareLocalHighball(config,{dataRoot, modulesRoot, log: s=>console.error(s)},
  path.join(dataRoot,'bottles'),{dir:runtimeDllDir,overrideDir:nexusRepair ?? runtimeOverrides,bundledDir},
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
} else if (mode === 'configure') {
  if (!profile) throw Error('Prepare the FH6 Store profile first');
  const result=spawnSync(runner.command,['regedit.exe','/S',path.join(root,'fh6-profile.reg')],
    {stdio:'inherit',env:process.env,timeout:60000});
  if (result.status !== 0) throw Error('Store-prefix font/display configuration failed');
  console.log('Configured only the Store prefix: Retina, CJK dialog font, native OptiScaler loader.');
} else if (mode === 'check' || mode === 'run') {
  const source = process.argv[3] ?? profile?.gamePath;
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
  if (!profile || profile.gamePath !== source) throw Error('Prepare the verified FH6 Store graphics profile first');
  for (const [file,expected] of Object.entries(profile.verifiedFiles)) {
    if(hashFile(file)!==expected)
      throw Error('FH6 Store component changed; reconfigure before launch: '+path.basename(file));
  }
  if (mode === 'check') {
    console.log(JSON.stringify({ready:true,packageVersion:profile.packageVersion,
      resolution:profile.resolution,upscaler:profile.upscaler,frameGeneration:profile.frameGeneration,
      priorFixes:runner.priorFixes,
      nexusRepair:Boolean(nexusRepair),
      cloudSave:'official Nexus/PlayFab auto sync; no save copied or conflict fabricated',
      gameplayVerified:false},null,2));
    process.exit(0);
  }
  // Normal Xodus launch performs Microsoft account/licence validation and its
  // package overlay. Never invoke a game image directly or substitute Steam files.
  const {readBundledBackend}=await import(path.join(modulesRoot,'bundled-backend.js'));
  const {cli}=await readBundledBackend('/Applications/MACxus.app/Contents/Resources/backend');
  const {startXodusService, cloudSaveRuntime}=await import(path.join(modulesRoot,'xodus-service.js'));
  if (!await cloudSaveRuntime(pointer.winePath)) throw Error('Expected the pinned cloud-save runtime');
  const service=await startXodusService(cli,os.tmpdir(),s=>console.error(s));
  let exitCode=1;
  try {
    const environment={...process.env, ...profile.environment, XODUS_SOCKET:service.socketPath,
      XODUS_BRIDGE_PORT:String(service.bridgePort),XODUS_BRIDGE_KEY:service.bridgeKey,XODUS_SAVE_SYNC:'auto'};
    // Steam is only the native shortcut host. Store ownership/saves come from
    // Microsoft/Xodus; do not start or sign into a second Windows Steam.
    for(const key of Object.keys(environment))
      if(/^(SteamAppId|SteamGameId|STEAM_COMPAT_|FEX_|HB_FEX_|HB_KK_|VKD3D_|MESA_|WINE_SOFTWARE_|WINE_EXCEPTION_BURST_|WINE_FIRST_FAULT_)/.test(key))delete environment[key];
    const proxy=path.resolve(path.dirname(pointer.winePath),'../share/xodus-nexus/compat/PlayFabGameSave.dll');
    if (!existsSync(proxy))throw Error('The PlayFab cloud-save proxy is missing');
    environment.NEXUS_PGS_PROXY=proxy;
    if (nexusRepair) {
      const directory = mkdtempSync(path.join(runner.compatibilityDataPath, 'drive_c', 'fh6-xbox-status-'));
      closeSync(openSync(path.join(directory, 'status.log'), 'wx', 0o600));
      environment.XUSER_AUTH_METADATA_LOG = `C:\\${path.basename(directory)}\\status.log`;
    }
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
