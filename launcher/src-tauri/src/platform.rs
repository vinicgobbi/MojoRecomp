// Host platform identity shared by the updater, the runtime launcher, and
// launcher self-replacement. Every file name, catalog name, and release target
// that differs between operating systems belongs here so the rest of the
// launcher can stay platform-neutral.

use std::path::{Path, PathBuf};
use std::process::Command;

#[cfg(not(any(windows, target_os = "linux")))]
compile_error!("MojoRecomp Launcher supports Windows and Linux hosts only");

#[cfg(windows)]
pub const HOST_PLATFORM: &str = "windows";
#[cfg(target_os = "linux")]
pub const HOST_PLATFORM: &str = "linux";

pub const HOST_ARCH: &str = "x86_64";

/// Platform suffix used in published artifact and package root names.
#[cfg(windows)]
pub const RELEASE_TARGET: &str = "windows-x64";
#[cfg(target_os = "linux")]
pub const RELEASE_TARGET: &str = "linux-x64";

/// Update catalog asset published with each release. Windows keeps the
/// original name so launchers that are already installed continue to find it.
#[cfg(windows)]
pub const UPDATE_CATALOG_ASSET: &str = "update-catalog.toml";
#[cfg(target_os = "linux")]
pub const UPDATE_CATALOG_ASSET: &str = "update-catalog-linux-x64.toml";

#[cfg(windows)]
pub const COT_RUNTIME_ENTRYPOINT: &str = "cot-runtime.exe";
#[cfg(target_os = "linux")]
pub const COT_RUNTIME_ENTRYPOINT: &str = "cot-runtime";

#[cfg(windows)]
pub const COT_RUNTIME_REQUIRED_FILES: &[&str] = &[
    COT_RUNTIME_ENTRYPOINT,
    "dxcompiler.dll",
    "dxil.dll",
    "mojorecomp-ffmpeg.dll",
    "mojorecomp-lzx.dll",
    "extract-xiso.exe",
];
// Provisional until the native Linux runtime package exists: the runtime
// translates shaders to SPIR-V, so DXIL validation is not part of the payload.
#[cfg(target_os = "linux")]
pub const COT_RUNTIME_REQUIRED_FILES: &[&str] = &[
    COT_RUNTIME_ENTRYPOINT,
    "libdxcompiler.so",
    "libmojorecomp-ffmpeg.so",
    "libmojorecomp-lzx.so",
    "extract-xiso",
];

/// Environment variable holding the user's home or profile directory.
#[cfg(windows)]
pub const USER_PROFILE_ENV: &str = "USERPROFILE";
#[cfg(target_os = "linux")]
pub const USER_PROFILE_ENV: &str = "HOME";

#[cfg(windows)]
pub const DXCOMPILER_LIBRARY: &str = "dxcompiler.dll";
#[cfg(target_os = "linux")]
pub const DXCOMPILER_LIBRARY: &str = "libdxcompiler.so";

/// LGPL libraries that users may replace through the lgpl-overrides folder.
#[cfg(windows)]
pub const REPLACEABLE_LGPL_LIBRARIES: &[&str] = &["mojorecomp-ffmpeg.dll", "mojorecomp-lzx.dll"];
#[cfg(target_os = "linux")]
pub const REPLACEABLE_LGPL_LIBRARIES: &[&str] =
    &["libmojorecomp-ffmpeg.so", "libmojorecomp-lzx.so"];

#[cfg(windows)]
pub const EXTRACT_XISO_EXECUTABLE: &str = "extract-xiso.exe";
#[cfg(target_os = "linux")]
pub const EXTRACT_XISO_EXECUTABLE: &str = "extract-xiso";

/// Name of the launcher artifact inside the update staging directories.
#[cfg(windows)]
pub const LAUNCHER_ARTIFACT_NAME: &str = "mojorecomp-launcher.exe";
#[cfg(target_os = "linux")]
pub const LAUNCHER_ARTIFACT_NAME: &str = "mojorecomp-launcher.AppImage";

/// Extension of the temporary self-update helper copy.
#[cfg(windows)]
pub const LAUNCHER_HELPER_EXTENSION: &str = ".exe";
#[cfg(target_os = "linux")]
pub const LAUNCHER_HELPER_EXTENSION: &str = ".AppImage";

/// Leading bytes of a launcher executable for this platform (PE or ELF).
#[cfg(windows)]
pub const LAUNCHER_EXECUTABLE_MAGIC: &[u8] = b"MZ";
#[cfg(target_os = "linux")]
pub const LAUNCHER_EXECUTABLE_MAGIC: &[u8] = b"\x7fELF";

pub fn is_runtime_entrypoint_name(name: &str) -> bool {
    #[cfg(windows)]
    {
        name.to_ascii_lowercase().ends_with(".exe")
    }
    #[cfg(target_os = "linux")]
    {
        let file_name = name.rsplit('/').next().unwrap_or(name);
        !file_name.is_empty() && !file_name.contains('.')
    }
}

/// The file that represents the installed launcher. For an AppImage this is
/// the image itself, not the binary inside its temporary read-only mount.
pub fn launcher_executable_path() -> Result<PathBuf, String> {
    #[cfg(target_os = "linux")]
    if let Some(path) = appimage_path() {
        return Ok(path);
    }
    std::env::current_exe().map_err(|error| format!("Could not resolve launcher executable: {error}"))
}

#[cfg(target_os = "linux")]
fn appimage_path() -> Option<PathBuf> {
    let path = PathBuf::from(std::env::var_os("APPIMAGE")?);
    (path.is_absolute() && path.is_file()).then_some(path)
}

/// Whether the running launcher is a packaged build that can replace itself.
pub fn launcher_supports_self_update(executable: &Path) -> bool {
    #[cfg(windows)]
    {
        executable
            .file_name()
            .and_then(|value| value.to_str())
            .is_some_and(|name| name.eq_ignore_ascii_case(LAUNCHER_ARTIFACT_NAME))
    }
    #[cfg(target_os = "linux")]
    {
        appimage_path().is_some_and(|path| path == executable)
    }
}

/// Marks a materialized payload file as executable. Windows has no mode bit.
pub fn make_executable(path: &Path) -> Result<(), String> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o755)).map_err(|error| {
            format!("Could not mark {} as executable: {error}", path.display())
        })?;
    }
    #[cfg(windows)]
    let _ = path;
    Ok(())
}

/// Opens a folder or URL with the desktop's default handler.
pub fn open_with_desktop(target: &std::ffi::OsStr) -> Result<(), String> {
    #[cfg(windows)]
    let program = "explorer.exe";
    #[cfg(target_os = "linux")]
    let program = "xdg-open";
    Command::new(program)
        .arg(target)
        .spawn()
        .map(|_| ())
        .map_err(|error| error.to_string())
}

/// Best-effort check for a process that has not exited yet. A zombie entry
/// counts as exited because it no longer runs code or holds files open.
#[cfg(target_os = "linux")]
pub fn process_is_running(pid: u32) -> bool {
    match std::fs::read_to_string(format!("/proc/{pid}/stat")) {
        Ok(stat) => stat
            .rsplit_once(')')
            .and_then(|(_, rest)| rest.split_whitespace().next())
            .is_some_and(|state| state != "Z" && state != "X"),
        Err(_) => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn runtime_entrypoint_list_contains_the_entrypoint() {
        assert!(COT_RUNTIME_REQUIRED_FILES.contains(&COT_RUNTIME_ENTRYPOINT));
        assert!(COT_RUNTIME_REQUIRED_FILES.contains(&EXTRACT_XISO_EXECUTABLE));
        assert!(COT_RUNTIME_REQUIRED_FILES.contains(&DXCOMPILER_LIBRARY));
        assert!(is_runtime_entrypoint_name(COT_RUNTIME_ENTRYPOINT));
        for library in REPLACEABLE_LGPL_LIBRARIES {
            assert!(COT_RUNTIME_REQUIRED_FILES.contains(library));
        }
    }

    #[cfg(target_os = "linux")]
    #[test]
    fn linux_entrypoints_have_no_extension() {
        assert!(is_runtime_entrypoint_name("cot-runtime"));
        assert!(is_runtime_entrypoint_name("bin/cot-runtime"));
        assert!(!is_runtime_entrypoint_name("cot-runtime.exe"));
        assert!(!is_runtime_entrypoint_name("libmojorecomp-lzx.so"));
    }

    #[cfg(target_os = "linux")]
    #[test]
    fn current_process_is_reported_as_running() {
        assert!(process_is_running(std::process::id()));
        assert!(!process_is_running(u32::MAX));
    }
}
