// Shared helpers for the launcher release (built in CI for every platform)
// and the COT runtime release (built locally where the game is available).
import { createHash, createPrivateKey, createPublicKey, sign as signBytes } from "node:crypto";
import { readFile, stat, writeFile } from "node:fs/promises";
import { homedir } from "node:os";
import { dirname, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { githubReleaseAssetUrls, isGitHubReleaseFeedUrl, validatePublicHttpsUrl } from "./release-metadata.mjs";

export const launcherRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
export const projectRoot = resolve(launcherRoot, "..");
export const releaseRoot = resolve(projectRoot, ".release");

const releasePublicKeyPath = resolve(launcherRoot, "release-signing-public-key.hex");
const defaultReleasePrivateKeyPath = resolve(projectRoot, "release-signing-private.pem");
const launcherSignatureMagic = Buffer.from("MOJORECOMP-LAUNCHER-SIG-V1", "ascii");
const launcherSignatureDomain = Buffer.from("MojoRecomp launcher executable signature v1\0", "utf8");

// Every platform the launcher is released for. The Windows catalog keeps its
// original asset name so installed launchers keep finding it.
export const releaseTargets = {
  windows: {
    platform: "windows",
    target: "windows-x64",
    catalogAsset: "update-catalog.toml",
    launcherExtension: ".exe",
    executableMagic: Buffer.from("MZ", "ascii"),
  },
  linux: {
    platform: "linux",
    target: "linux-x64",
    catalogAsset: "update-catalog-linux-x64.toml",
    launcherExtension: ".AppImage",
    executableMagic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]),
  },
};

export function hostReleaseTarget() {
  if (process.arch !== "x64") throw new Error(`Unsupported release architecture: ${process.arch}`);
  if (process.platform === "win32") return releaseTargets.windows;
  if (process.platform === "linux") return releaseTargets.linux;
  throw new Error(`Unsupported release platform: ${process.platform}`);
}

export function fail(message) {
  throw new Error(message);
}

export function quotedValue(text, key, label = "version.toml") {
  const match = text.match(new RegExp(`^${key}\\s*=\\s*"([^"]+)"`, "m"));
  if (!match) fail(`Missing ${key} in ${label}`);
  return match[1];
}

export async function sha256(path) {
  return createHash("sha256").update(await readFile(path)).digest("hex");
}

export async function requireFile(path, label) {
  const info = await stat(path).catch(() => null);
  if (!info?.isFile()) fail(`${label} is missing: ${path}`);
}

export async function readVersions() {
  const text = await readFile(resolve(projectRoot, "version.toml"), "utf8");
  return {
    suite: quotedValue(text, "suite"),
    launcher: quotedValue(text, "launcher"),
    cotRuntime: quotedValue(text, "cot_runtime"),
    cotLocalizationPack: quotedValue(text, "cot_localization_pack"),
  };
}

export async function rejectLocalBuildPaths(path, label) {
  const bytes = await readFile(path);
  for (const localPath of [projectRoot, homedir()].filter(Boolean)) {
    for (const spelling of [localPath, localPath.replaceAll("\\", "/")]) {
      if (
        bytes.includes(Buffer.from(spelling, "utf8")) ||
        bytes.includes(Buffer.from(spelling, "utf16le"))
      ) {
        fail(`${label} contains a local build path: ${localPath}`);
      }
    }
  }
}

export function runProgram(program, args, cwd) {
  const result = spawnSync(program, args, { cwd, encoding: "utf8", windowsHide: true });
  if (result.error) fail(`Could not start ${program}: ${result.error.message}`);
  if (result.status !== 0) {
    fail(`${program} failed (${result.status}): ${(result.stderr || result.stdout).trim()}`);
  }
  return result.stdout.trim();
}

/** The embedded release public key, or "" in unsigned release mode. */
export async function releasePublicKeyHex() {
  const value = (await readFile(releasePublicKeyPath, "utf8")).trim().toLowerCase();
  if (value && !/^[0-9a-f]{64}$/.test(value)) {
    fail("release-signing-public-key.hex must be empty or contain one 32-byte Ed25519 public key");
  }
  return value;
}

/**
 * The Ed25519 private key that signs release artifacts, or null when the
 * launcher embeds no public key (unsigned release mode).
 */
export async function releaseSigningKey() {
  const expectedPublicKey = await releasePublicKeyHex();
  if (!expectedPublicKey) return null;
  const privateKeyPath = process.env.MOJORECOMP_RELEASE_SIGNING_KEY
    ? resolve(process.env.MOJORECOMP_RELEASE_SIGNING_KEY)
    : defaultReleasePrivateKeyPath;
  const privatePem = await readFile(privateKeyPath, "utf8").catch(() => null);
  if (!privatePem) {
    fail(
      `Release signing key is missing: ${privateKeyPath}. ` +
        "Set MOJORECOMP_RELEASE_SIGNING_KEY, or empty release-signing-public-key.hex for unsigned releases.",
    );
  }
  const privateKey = createPrivateKey(privatePem);
  if (privateKey.asymmetricKeyType !== "ed25519") {
    fail("The release signing key must be an Ed25519 private key");
  }
  const publicDer = createPublicKey(privateKey).export({ format: "der", type: "spki" });
  if (publicDer.subarray(publicDer.length - 32).toString("hex") !== expectedPublicKey) {
    fail("The release signing key does not match the public key embedded in the launcher");
  }
  return privateKey;
}

/** Writes the launcher release artifact, with a signature trailer when a key is given. */
export async function writeLauncherArtifact(source, destination, version, target, signingKey) {
  const payload = await readFile(source);
  if (!payload.subarray(0, target.executableMagic.length).equals(target.executableMagic)) {
    fail(`Launcher build is not a ${target.platform} executable: ${source}`);
  }
  if (!signingKey) {
    await writeFile(destination, payload, { mode: 0o755 });
    return;
  }
  const versionBytes = Buffer.from(version, "utf8");
  if (versionBytes.length === 0 || versionBytes.length > 0xffff) {
    fail("Launcher version is too long for the signed executable trailer");
  }
  const payloadHash = createHash("sha256").update(payload).digest();
  const message = Buffer.concat([launcherSignatureDomain, versionBytes, Buffer.from([0]), payloadHash]);
  const signature = signBytes(null, message, signingKey);
  if (signature.length !== 64) fail("Ed25519 produced an invalid launcher signature length");
  const versionLength = Buffer.alloc(2);
  versionLength.writeUInt16LE(versionBytes.length);
  await writeFile(
    destination,
    Buffer.concat([payload, versionBytes, signature, versionLength, launcherSignatureMagic]),
    { mode: 0o755 },
  );
}

export function launcherCatalogBlock(release, target) {
  return `[[release]]
id = "launcher"
kind = "launcher"
version = ${JSON.stringify(release.version)}
platform = ${JSON.stringify(target.platform)}
arch = "x86_64"
url = ${JSON.stringify(release.url)}
size = ${release.size}
sha256 = ${JSON.stringify(release.sha256)}
published = ${JSON.stringify(release.published)}
notes_url = ${JSON.stringify(release.notesUrl)}
package = "portable-exe"

[release.compatibility]`;
}

/** Splits an update catalog into its [[release]] blocks with their identity. */
export function catalogReleaseBlocks(catalogText) {
  const normalized = (catalogText ?? "").replace(/\r\n/g, "\n");
  return normalized
    .split(/(?=^\[\[release\]\]\s*$)/m)
    .map((block) => block.trim())
    .filter((block) => block.startsWith("[[release]]"))
    .map((text) => {
      const field = (key) => text.match(new RegExp(`^${key}\\s*=\\s*"([^"]+)"`, "m"))?.[1] ?? null;
      return {
        text,
        id: field("id"),
        kind: field("kind"),
        version: field("version"),
        platform: field("platform"),
      };
    });
}

/**
 * The newest published copy of a catalog asset in the configured release feed,
 * or null when no release has published it yet. A feed that cannot be read
 * fails the release instead of silently dropping published components.
 */
export async function fetchPublishedCatalog(feedUrl, assetName) {
  validatePublicHttpsUrl(feedUrl, "release feed URL");
  const headers = {
    Accept: "application/vnd.github+json",
    "User-Agent": "MojoRecomp-release-builder",
  };
  if (process.env.GITHUB_TOKEN) headers.Authorization = `Bearer ${process.env.GITHUB_TOKEN}`;
  const request = (url) => fetch(url, {
    redirect: "follow",
    cache: "no-store",
    signal: AbortSignal.timeout(15000),
    headers,
  });
  const response = await request(feedUrl);
  if (!response.ok) fail(`Release feed is unavailable (${response.status}): ${feedUrl}`);
  if (!isGitHubReleaseFeedUrl(feedUrl)) return response.text();
  const [assetUrl] = githubReleaseAssetUrls(await response.json(), assetName);
  if (!assetUrl) return null;
  const asset = await request(assetUrl);
  if (!asset.ok) fail(`Published ${assetName} is unavailable (${asset.status}): ${assetUrl}`);
  validatePublicHttpsUrl(asset.url, `resolved ${assetName} URL`);
  return asset.text();
}
