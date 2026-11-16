// After FH6 exits, Wine's background services must not keep the Steam app alive.
// Only the wineserver -w client is stopped. Never stop the server or a game.
import {spawn, spawnSync} from 'node:child_process';
import {pathToFileURL} from 'node:url';

function gameRunning() {
  const result = spawnSync('/bin/ps', ['-axo', 'comm='], {encoding: 'utf8'});
  if (result.status !== 0) throw Error('Could not inspect game processes');
  return result.stdout.split('\n').some(line => /(?:^|[\\/])forzahorizon6\.exe$/i.test(line.trim()));
}

export function waitForPrefix({server, graceMs = 15000, pollMs = 1000,
  isGameRunning = gameRunning, log = console.error}) {
  return new Promise((resolve, reject) => {
    const child = spawn(server, ['-w'], {stdio: 'ignore'});
    let absentSince;
    let shortened = false;
    let uncertain = false;
    const timer = setInterval(() => {
      try {
        if (isGameRunning()) { absentSince = undefined; return; }
        absentSince ??= Date.now();
        if (Date.now() - absentSince >= graceMs && !shortened) {
          shortened = true;
          log('FH6 exited; releasing background-prefix wait. Wine services remain running.');
          child.kill('SIGTERM');
        }
      } catch {
        // Uncertain process state must never be treated as an exited game.
        absentSince = undefined;
        if (!uncertain) log('FH6 process check unavailable; retaining prefix wait.');
        uncertain = true;
      }
    }, pollMs);
    const stop = () => child.kill('SIGTERM');
    process.once('SIGINT', stop);
    process.once('SIGTERM', stop);
    const cleanup = () => {
      clearInterval(timer);
      process.removeListener('SIGINT', stop);
      process.removeListener('SIGTERM', stop);
    };
    child.once('error', error => { cleanup(); reject(error); });
    child.once('exit', (code, signal) => {
      cleanup();
      resolve(shortened && signal === 'SIGTERM' ? 0 : (code ?? 143));
    });
  });
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const server = process.argv[2];
  if (!server?.startsWith('/') || process.argv.length !== 3)
    throw Error('Expected the checked absolute wineserver path');
  process.exitCode = await waitForPrefix({server});
}
