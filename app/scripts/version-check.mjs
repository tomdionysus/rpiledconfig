#!/usr/bin/env node
/**
 * Checks that every stated version agrees: package.json, package-lock.json,
 * app.json, a generated android/ project, the root README's `*vX.Y.Z*` line
 * and the git tag. The daemon's CMake checks its own against the same README.
 *
 * `android.versionCode` must be major*10000 + minor*100 + patch: Android
 * compares only that integer and ignores the version name.
 */
import { readFileSync } from 'node:fs';
import { execSync } from 'node:child_process';

const read = (path) => JSON.parse(readFileSync(new URL(`../${path}`, import.meta.url), 'utf8'));

const pkg = read('package.json');
const app = read('app.json').expo;
const problems = [];

if (pkg.version !== app.version) {
  problems.push(`package.json says ${pkg.version}, app.json says ${app.version}`);
}

// `npm install` rewrites the lockfile's version only when something else changes.
let lock;
try {
  lock = read('package-lock.json');
} catch {
  lock = undefined;
}
if (lock && lock.version !== pkg.version) {
  problems.push(`package-lock.json says ${lock.version}, package.json says ${pkg.version} (run npm install)`);
}

// Gradle reads the version from the prebuilt `android/app/build.gradle`, not
// app.json, so a bump without a fresh prebuild ships the old version. android/
// is gitignored; only a present, disagreeing one is a problem.
let gradle;
try {
  gradle = readFileSync(new URL('../android/app/build.gradle', import.meta.url), 'utf8');
} catch {
  gradle = undefined;
}
if (gradle) {
  const gradleName = /versionName\s+"([^"]+)"/.exec(gradle)?.[1];
  const gradleCode = Number(/versionCode\s+(\d+)/.exec(gradle)?.[1]);
  if (gradleName !== app.version || gradleCode !== app.android?.versionCode) {
    problems.push(
      `android/app/build.gradle says ${gradleName ?? 'unset'} (versionCode ${Number.isNaN(gradleCode) ? 'unset' : gradleCode}); ` +
        `run npx expo prebuild --platform android before building`,
    );
  }
}

// The first non-blank line after the root README's title is `*vX.Y.Z*`, with
// an optional qualifier such as `(in development)` inside the emphasis.
const readme = readFileSync(new URL('../../README.md', import.meta.url), 'utf8');
const readmeLine = readme.split('\n').slice(1).find((line) => line.trim() !== '')?.trim();
const readmeVersion = /^\*v(\d+\.\d+\.\d+)(?: [^*]*)?\*$/.exec(readmeLine ?? '')?.[1];
if (readmeVersion !== pkg.version) {
  problems.push(
    readmeVersion
      ? `../README.md says v${readmeVersion}, package.json says ${pkg.version}`
      : `../README.md has no *v${pkg.version}* line under its title`,
  );
}

const match = /^(\d+)\.(\d+)\.(\d+)$/.exec(pkg.version);
if (!match) {
  problems.push(`${pkg.version} is not a bare x.y.z version`);
} else {
  const [, major, minor, patch] = match.map(Number);
  const expected = major * 10000 + minor * 100 + patch;
  if (app.android?.versionCode !== expected) {
    problems.push(`android.versionCode is ${app.android?.versionCode ?? 'unset'}, expected ${expected} for ${pkg.version}`);
  }
}

// Checked only when this commit is tagged.
let tag;
try {
  tag = execSync('git describe --tags --exact-match', { stdio: ['ignore', 'pipe', 'ignore'] }).toString().trim();
} catch {
  tag = undefined;
}
if (tag && tag !== pkg.version) {
  problems.push(`this commit is tagged ${tag} but package.json says ${pkg.version}`);
}

if (problems.length > 0) {
  console.error('Version mismatch:');
  for (const problem of problems) console.error(`  - ${problem}`);
  process.exit(1);
}

console.log(`${pkg.version} (versionCode ${app.android.versionCode})${tag ? `, tagged ${tag}` : ''}: consistent`);
