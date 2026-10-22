// Packages the launcher built on this host for release. Run after
// build-tauri-release.mjs on each platform; assemble-launcher-release.mjs then
// combines every platform into one release with its update catalogs.
import { mkdir, readFile, readdir, rm, stat, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import {
  fail,
  hostReleaseTarget,
  launcherRoot,
  quotedValue,
  readVersions,
  rejectLocalBuildPaths,
  releaseRoot,
  releaseSigningKey,
  requireFile,
  sha256,
  writeLauncherArtifact,
} from "./release-common.mjs";

const target = hostReleaseTarget();
const versions = await readVersions();
if (versions.suite !== versions.launcher) {
  fail(`Suite ${versions.suite} and launcher ${versions.launcher} must match`);
}
const suiteToml = await readFile(resolve(launcherRoot, "resources/suite.toml"), "utf8");
if (quotedValue(suiteToml, "version", "suite.toml") !== versions.suite) {
  fail("resources/suite.toml does not match version.toml");
}

const releaseBuild = resolve(launcherRoot, "src-tauri/target/release");
const executable = resolve(
  releaseBuild,
  target.platform === "windows" ? "mojorecomp-launcher.exe" : "mojorecomp-launcher",
);
await requireFile(executable, "Launcher release build");
await rejectLocalBuildPaths(executable, "Launcher release build");

let source = executable;
if (target.platform === "linux") {
  const bundleRoot = resolve(releaseBuild, "bundle/appimage");
  const images = (await readdir(bundleRoot).catch(() => []))
    .filter((name) => name.endsWith(`_${versions.launcher}_amd64.AppImage`));
  if (images.length !== 1) {
    fail(`Expected one ${versions.launcher} AppImage in ${bundleRoot}, found ${images.length}`);
  }
  source = resolve(bundleRoot, images[0]);
}

const outputRoot = resolve(releaseRoot, "launcher", target.target);
await rm(outputRoot, { recursive: true, force: true });
await mkdir(outputRoot, { recursive: true });
const file = `MojoRecomp-Launcher-${versions.launcher}-${target.target}${target.launcherExtension}`;
const destination = resolve(outputRoot, file);
const signingKey = await releaseSigningKey();
await writeLauncherArtifact(source, destination, versions.launcher, target, signingKey);

const metadata = {
  schema_version: 1,
  platform: target.platform,
  target: target.target,
  version: versions.launcher,
  file,
  size: (await stat(destination)).size,
  sha256: await sha256(destination),
  signed: Boolean(signingKey),
};
await writeFile(resolve(outputRoot, "launcher.json"), `${JSON.stringify(metadata, null, 2)}\n`, "utf8");
console.log(`Launcher ${metadata.version} for ${metadata.target}: ${file}`);
console.log(`SHA-256: ${metadata.sha256}${signingKey ? "" : " (unsigned release mode)"}`);
