import assert from "node:assert/strict";
import { generateKeyPairSync, createHash } from "node:crypto";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { catalogReleaseBlocks, launcherCatalogBlock, releaseTargets, writeLauncherArtifact } from "./release-common.mjs";

const publishedCatalog = `schema_version = 1
channel = "development"

[[release]]
id = "launcher"
kind = "launcher"
version = "1.1.0"
platform = "windows"
arch = "x86_64"
url = "https://github.com/example/MojoRecomp/releases/download/v1.1.0/MojoRecomp-Launcher-1.1.0-windows-x64.exe"
size = 10
sha256 = "${"a".repeat(64)}"
published = "2026-10-06"
notes_url = "https://github.com/example/MojoRecomp/releases/tag/v1.1.0"
package = "portable-exe"

[release.compatibility]

[[release]]
id = "runtime.cot"
kind = "runtime"
version = "0.2.0-alpha"
platform = "windows"
arch = "x86_64"
url = "https://github.com/example/MojoRecomp/releases/download/v1.1.0/runtime.zip"
size = 20
sha256 = "${"b".repeat(64)}"
published = "2026-10-06"
notes_url = "https://github.com/example/MojoRecomp/releases/tag/v1.1.0"
package = "zip"
unpacked_size = 30
entrypoint = "cot-runtime.exe"
required_files = ["cot-runtime.exe", "mojorecomp-package.toml"]
game_id = "cot"

[release.compatibility]
min_launcher = "1.1.0"

[[release.compatibility.require]]
id = "language.cot.pt-br"
min_version = "1.0.0"
`;

test("catalog blocks keep nested compatibility tables with their release", () => {
  const blocks = catalogReleaseBlocks(publishedCatalog.replace(/\n/g, "\r\n"));
  assert.deepEqual(
    blocks.map(({ id, kind, version, platform }) => ({ id, kind, version, platform })),
    [
      { id: "launcher", kind: "launcher", version: "1.1.0", platform: "windows" },
      { id: "runtime.cot", kind: "runtime", version: "0.2.0-alpha", platform: "windows" },
    ],
  );
  assert.match(blocks[1].text, /\[\[release\.compatibility\.require\]\]\nid = "language\.cot\.pt-br"/);
  assert.ok(!blocks[0].text.includes("runtime.cot"));
  assert.deepEqual(catalogReleaseBlocks(null), []);
});

test("launcher catalog blocks name the target platform", () => {
  const release = {
    version: "1.2.0",
    url: "https://example.com/launcher",
    size: 1,
    sha256: "c".repeat(64),
    published: "2026-10-22",
    notesUrl: "https://example.com/notes",
  };
  const block = launcherCatalogBlock(release, releaseTargets.linux);
  assert.match(block, /^platform = "linux"$/m);
  assert.match(block, /^package = "portable-exe"$/m);
  assert.equal(catalogReleaseBlocks(block)[0].kind, "launcher");
});

test("launcher artifacts are copied unsigned or receive a signature trailer", async () => {
  const root = await mkdtemp(join(tmpdir(), "mojorecomp-release-common-"));
  try {
    const source = join(root, "launcher");
    const payload = Buffer.concat([releaseTargets.linux.executableMagic, Buffer.from("payload")]);
    await writeFile(source, payload);

    const unsigned = join(root, "unsigned.AppImage");
    await writeLauncherArtifact(source, unsigned, "1.2.0", releaseTargets.linux, null);
    assert.deepEqual(await readFile(unsigned), payload);

    const { privateKey } = generateKeyPairSync("ed25519");
    const signed = join(root, "signed.AppImage");
    await writeLauncherArtifact(source, signed, "1.2.0", releaseTargets.linux, privateKey);
    const bytes = await readFile(signed);
    assert.deepEqual(bytes.subarray(0, payload.length), payload);
    assert.ok(bytes.subarray(-26).equals(Buffer.from("MOJORECOMP-LAUNCHER-SIG-V1", "ascii")));
    assert.notEqual(createHash("sha256").update(bytes).digest("hex"), createHash("sha256").update(payload).digest("hex"));

    await assert.rejects(
      writeLauncherArtifact(source, join(root, "wrong.exe"), "1.2.0", releaseTargets.windows, null),
      /not a windows executable/,
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
