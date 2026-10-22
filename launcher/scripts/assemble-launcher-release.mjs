// Combines the per-platform outputs of package-launcher-release.mjs into one
// launcher release: the launcher artifacts, one update catalog per platform,
// and SHA256SUMS.txt.
//
// Each catalog lists the new launcher and carries every runtime release from
// the newest catalog already published for that platform, so a launcher-only
// release never hides published runtimes. Runtime releases do the reverse.
import { copyFile, mkdir, readFile, readdir, rm, stat, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import {
  catalogReleaseBlocks,
  fail,
  fetchPublishedCatalog,
  launcherCatalogBlock,
  launcherRoot,
  quotedValue,
  readVersions,
  releaseRoot,
  releaseTargets,
  sha256,
} from "./release-common.mjs";
import {
  normalizedHttpsBase,
  validatePublicHttpsUrl,
  validateReleaseChannel,
  validateReleaseDate,
} from "./release-metadata.mjs";

const versions = await readVersions();
const suiteToml = await readFile(resolve(launcherRoot, "resources/suite.toml"), "utf8");
const releaseChannel = validateReleaseChannel(quotedValue(suiteToml, "release_channel", "suite.toml"));
const feedUrl = validatePublicHttpsUrl(
  quotedValue(suiteToml, "update_catalog", "suite.toml"),
  "suite.toml update_catalog",
);
const updateBaseUrl = normalizedHttpsBase(process.env.MOJORECOMP_UPDATE_BASE_URL || "", "MOJORECOMP_UPDATE_BASE_URL");
const notesUrl = validatePublicHttpsUrl(process.env.MOJORECOMP_UPDATE_NOTES_URL || "", "MOJORECOMP_UPDATE_NOTES_URL");
if (!updateBaseUrl || !notesUrl) {
  fail("MOJORECOMP_UPDATE_BASE_URL and MOJORECOMP_UPDATE_NOTES_URL are required");
}
const published = validateReleaseDate(process.env.MOJORECOMP_RELEASE_DATE || new Date().toISOString().slice(0, 10));

const inputRoot = resolve(releaseRoot, "launcher");
const outputRoot = resolve(releaseRoot, "launcher-release");
await rm(outputRoot, { recursive: true, force: true });
await mkdir(outputRoot, { recursive: true });

const checksums = [];
const platformDirectories = (await readdir(inputRoot, { withFileTypes: true }).catch(() => []))
  .filter((entry) => entry.isDirectory())
  .map((entry) => entry.name);
for (const target of Object.values(releaseTargets)) {
  if (!platformDirectories.includes(target.target)) {
    fail(`Launcher build for ${target.target} is missing from ${inputRoot}`);
  }
  const directory = resolve(inputRoot, target.target);
  const metadata = JSON.parse(await readFile(resolve(directory, "launcher.json"), "utf8"));
  if (
    metadata.schema_version !== 1
    || metadata.platform !== target.platform
    || metadata.target !== target.target
    || metadata.version !== versions.launcher
    || metadata.file !== `MojoRecomp-Launcher-${versions.launcher}-${target.target}${target.launcherExtension}`
  ) {
    fail(`Launcher metadata for ${target.target} does not match launcher ${versions.launcher}`);
  }
  const artifact = resolve(outputRoot, metadata.file);
  await copyFile(resolve(directory, metadata.file), artifact);
  const actual = { size: (await stat(artifact)).size, sha256: await sha256(artifact) };
  if (actual.size !== metadata.size || actual.sha256 !== metadata.sha256) {
    fail(`Launcher artifact for ${target.target} does not match its metadata`);
  }
  checksums.push({ file: metadata.file, sha256: actual.sha256 });

  const previous = await fetchPublishedCatalog(feedUrl, target.catalogAsset);
  const carried = catalogReleaseBlocks(previous)
    .filter((block) => block.kind === "runtime" && block.platform === target.platform);
  const launcherBlock = launcherCatalogBlock(
    {
      version: versions.launcher,
      url: `${updateBaseUrl}/${metadata.file}`,
      size: actual.size,
      sha256: actual.sha256,
      published,
      notesUrl,
    },
    target,
  );
  const catalog = `schema_version = 1
channel = ${JSON.stringify(releaseChannel)}

${[launcherBlock, ...carried.map((block) => block.text)].join("\n\n")}
`;
  const catalogPath = resolve(outputRoot, target.catalogAsset);
  await writeFile(catalogPath, catalog, "utf8");
  checksums.push({ file: target.catalogAsset, sha256: await sha256(catalogPath) });
  console.log(
    `${target.catalogAsset}: launcher ${versions.launcher}, ${carried.length} carried runtime release(s)` +
      (metadata.signed ? "" : " (unsigned)"),
  );
}

await writeFile(
  resolve(outputRoot, "SHA256SUMS.txt"),
  `${checksums.map((entry) => `${entry.sha256}  ${entry.file}`).join("\n")}\n`,
  "utf8",
);
console.log(`Launcher release assembled: ${outputRoot}`);
