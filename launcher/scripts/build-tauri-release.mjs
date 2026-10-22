import { spawnSync } from "node:child_process";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const launcherRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const projectRoot = resolve(launcherRoot, "..");
const isWindows = process.platform === "win32";
const userProfileVariable = isWindows ? process.env.USERPROFILE : process.env.HOME;
const userProfile = userProfileVariable ? resolve(userProfileVariable) : null;
const unitSeparator = String.fromCharCode(0x1f);

const rustFlags = [];
const sourceReplacement = isWindows ? "C:\\src\\MojoRecomp" : "/src/MojoRecomp";
const userReplacement = isWindows ? "C:\\build-user" : "/build-user";
for (const [source, replacement] of [
  [projectRoot, sourceReplacement],
  [userProfile, userReplacement],
]) {
  if (!source) continue;
  rustFlags.push("--remap-path-prefix", `${source}=${replacement}`);
}

// MSVC and GCC/Clang spell the C/C++ source path remapping flag differently.
const pathMapFlags = !userProfile
  ? ""
  : isWindows
    ? ` /pathmap:${userProfile}=${userReplacement}`
    : ` -ffile-prefix-map=${userProfile}=${userReplacement}`;
const env = {
  ...process.env,
  CARGO_ENCODED_RUSTFLAGS: rustFlags.join(unitSeparator),
  CFLAGS: `${process.env.CFLAGS ?? ""}${pathMapFlags}`.trim(),
  CXXFLAGS: `${process.env.CXXFLAGS ?? ""}${pathMapFlags}`.trim(),
};

const tauri = resolve(launcherRoot, "node_modules/@tauri-apps/cli/tauri.js");
const result = spawnSync(process.execPath, [tauri, "build", "--no-bundle"], {
  cwd: launcherRoot,
  env,
  stdio: "inherit",
  windowsHide: true,
});

if (result.error) {
  throw result.error;
}
process.exit(result.status ?? 1);
