// aot_cache.cpp -- see aot_cache.h.

#include "aot_cache.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
// MinGW has no <sys/wait.h>: std::system() already returns the child's exit
// code directly there, so there is nothing to decode. <direct.h> supplies the
// one-argument _mkdir, and <stdlib.h> the _fullpath that stands in for
// realpath().
#  include <direct.h>
#else
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace godot {
namespace aot {

namespace {

// --- mkdir -p ---------------------------------------------------------------

// Windows accepts both separators and its mkdir takes no mode (there are no
// POSIX permission bits to apply), so the component split has to recognise '\\'
// too -- Godot hands us '/' paths, but an absolute path assembled from a
// Windows env var or _fullpath comes back with backslashes.
bool mkdir_one(const std::string &p) {
#ifdef _WIN32
    return _mkdir(p.c_str()) == 0 || errno == EEXIST;
#else
    return mkdir(p.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

bool is_sep(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

// True for a prefix that names a filesystem root rather than a directory we
// could create: "/" everywhere, plus "C:" / "C:/" on Windows. Calling mkdir on
// a drive root fails with EACCES rather than EEXIST, which would abort the walk.
bool is_root_prefix(const std::string &acc) {
    if (acc.empty() || acc == "/") return true;
#ifdef _WIN32
    if (acc.size() == 2 && acc[1] == ':') return true;
    if (acc.size() == 3 && acc[1] == ':' && is_sep(acc[2])) return true;
#endif
    return false;
}

bool mkdir_p(const std::string &path) {
    if (path.empty()) return false;
    std::string acc;
    acc.reserve(path.size());
    for (size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || is_sep(path[i])) {
            if (!is_root_prefix(acc) && !mkdir_one(acc)) {
                return false;
            }
        }
        if (i < path.size()) acc.push_back(path[i]);
    }
    return true;
}

bool path_exists(const std::string &p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

bool path_is_file(const std::string &p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool write_file(const std::string &path, const std::string &content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(content.data(), (std::streamsize)content.size());
    return out.good();
}

bool write_file_bytes(const std::string &path, const char *data, size_t len) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data, (std::streamsize)len);
    return out.good();
}

// FNV-1a 64-bit. Not cryptographic; collision-resistant enough for a
// per-project cache keyed on source bytes + compiler identity.
constexpr uint64_t FNV64_OFFSET = 0xcbf29ce484222325ULL;
constexpr uint64_t FNV64_PRIME  = 0x100000001b3ULL;

uint64_t fnv1a64_update(uint64_t h, const void *data, size_t len) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= FNV64_PRIME;
    }
    return h;
}

std::string hex_u64(uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
    return std::string(buf);
}

// Reduce a tur binary path to (realpath, mtime). When realpath fails we fall
// back to the literal path -- a bare "tur" still hashes to something stable
// per host, which is the best we can do without invoking the binary.
std::string tur_identity(const std::string &tur_bin) {
    char real[4096];
    std::string ident;
#ifdef _WIN32
    // _fullpath is the CRT's realpath: destination first, and it canonicalises
    // lexically without requiring the path to exist. The stat() below is what
    // decides whether we actually found the binary, so the weaker guarantee
    // costs nothing here.
    if (_fullpath(real, tur_bin.c_str(), sizeof(real))) {
#else
    if (realpath(tur_bin.c_str(), real)) {
#endif
        ident.assign(real);
    } else {
        ident = tur_bin;
    }
    struct stat st;
    if (::stat(ident.c_str(), &st) == 0) {
        char tail[64];
        std::snprintf(tail, sizeof(tail), "|mtime=%lld|size=%lld",
                      (long long)st.st_mtime, (long long)st.st_size);
        ident.append(tail);
    }
    return ident;
}

// Quote a path for the shell std::system() will actually use. We invoke the
// compiler via a shell so stdout/stderr capture composes; we never embed
// user-supplied strings without quoting.
//
// On Windows std::system() runs `cmd.exe /c`, which does NOT understand POSIX
// single-quoting -- it would pass the quote characters through as part of the
// filename, so every path containing one (and every path at all, since the
// quotes become literal) is mangled. cmd.exe quotes with double quotes, inside
// which its metacharacters (&, |, <, >, ^) lose their meaning. A literal double
// quote in a path is impossible on Windows -- the filesystem forbids it -- so
// there is no escape case to handle, only one to reject.
std::string sh_quote(const std::string &s) {
    std::string out;
#ifdef _WIN32
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        // Cannot occur in a Windows path; drop rather than emit an unbalanced
        // quote that would silently re-parse the whole command line.
        if (c == '"') continue;
        out.push_back(c);
    }
    out.push_back('"');
#else
    out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') out.append("'\\''");
        else            out.push_back(c);
    }
    out.push_back('\'');
#endif
    return out;
}

// Wrap a fully-assembled command for std::system().
//
// cmd.exe /c strips the first and last character when the command string both
// begins and ends with a double quote. Our command begins with a quoted tur
// path, so without a second enclosing pair the opening quote of the executable
// is eaten and a path containing spaces splits. Wrapping the whole line is the
// documented idiom. No-op off Windows.
std::string shell_command(const std::string &cmd) {
#ifdef _WIN32
    return "\"" + cmd + "\"";
#else
    return cmd;
#endif
}

// Derive a stable, filesystem-safe module name from the script's basename
// (without the .tur suffix). We coerce non-[A-Za-z0-9_-] to '_' so a path
// like "Player Idle.tur" doesn't break the staged module name.
std::string module_name_for(const std::string &script_path) {
    std::string base;
    size_t slash = script_path.find_last_of('/');
    base = (slash == std::string::npos) ? script_path
                                         : script_path.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base.resize(dot);
    if (base.empty()) base = "script";
    for (char &c : base) {
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            c = '_';
        }
    }
    return base;
}

// Minimal build.tur for the staged project. We omit :exports -- the user's
// script declares its own (defmodule ... :exports [...]) and we trust the
// compiler's exported-defn detection. The staged package name embeds the
// hash so log lines name the cache slot, not "script".
std::string make_build_tur(const std::string &pkg_name) {
    std::string out;
    out.append("(defpackage ").append(pkg_name).append("\n");
    out.append("  :name \"").append(pkg_name).append("\"\n");
    out.append("  :version \"0.1.0\")\n");
    return out;
}

} // namespace

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------

std::string compute_script_hash(const std::string &script_path,
                                const char *source_bytes, size_t source_len,
                                const std::string &tur_bin) {
    std::string ident = tur_identity(tur_bin);
    uint64_t h = FNV64_OFFSET;
    h = fnv1a64_update(h, ident.data(), ident.size());
    // Domain separator so script_path / source / ident are not silently
    // concatenable across boundaries.
    h = fnv1a64_update(h, "|path=", 6);
    h = fnv1a64_update(h, script_path.data(), script_path.size());
    h = fnv1a64_update(h, "|src=", 5);
    if (source_bytes && source_len > 0) {
        h = fnv1a64_update(h, source_bytes, source_len);
    }
    return hex_u64(h);
}

std::string resolve_tur_bin(const std::string &project_setting_override) {
    const char *env = std::getenv("TUR_BIN");
    if (env && *env) return std::string(env);
    if (!project_setting_override.empty()) return project_setting_override;
    return std::string("tur");
}

std::string cache_root_for(const std::string &godot_project_dir) {
    if (godot_project_dir.empty()) {
        // Windows sets neither TMPDIR nor has a /tmp, so the POSIX pair would
        // fall through to an unwritable literal path. TEMP/TMP are what the
        // CRT and every Windows tool use.
        const char *tmp = std::getenv("TMPDIR");
#ifdef _WIN32
        if (!tmp || !*tmp) tmp = std::getenv("TEMP");
        if (!tmp || !*tmp) tmp = std::getenv("TMP");
        if (!tmp || !*tmp) tmp = ".";
#else
        if (!tmp || !*tmp) tmp = "/tmp";
#endif
        return std::string(tmp) + "/turmeric-godot-cache";
    }
    return godot_project_dir + "/.godot/turmeric-cache";
}

// Basename the platform's shared library takes for package `pkg`. Mirrors
// TUR_SHLIB_PREFIX / TUR_SHLIB_EXT in the compiler's src/platform_fs.h -- PE
// has no `lib` convention, and a `libfoo.so` on Windows is a file the Godot
// GDExtension loader will not recognise as a module.
static std::string shlib_basename(const std::string &pkg) {
#ifdef _WIN32
    return pkg + ".dll";
#else
    return "lib" + pkg + ".so";
#endif
}

BuildOutputs predict_outputs(const std::string &godot_project_dir,
                              const std::string &script_path,
                              const char *source_bytes, size_t source_len,
                              const std::string &tur_bin) {
    BuildOutputs o;
    const std::string hash = compute_script_hash(script_path, source_bytes,
                                                  source_len, tur_bin);
    const std::string root  = cache_root_for(godot_project_dir);
    const std::string stage = root + "/" + hash;
    const std::string pkg   = std::string("tg_script_") + hash.substr(0, 12);
    const std::string lib_path = stage + "/build/lib/" + shlib_basename(pkg);
    o.stage_dir     = stage;
    o.lib_path      = lib_path;
    o.manifest_path = lib_path + ".manifest";
    o.metadata_path = stage + "/exports.metadata";
    o.pkg_name      = pkg;
    o.cache_hit     = false;
    return o;
}

bool ensure_built(const std::string &godot_project_dir,
                  const std::string &script_path,
                  const char *source_bytes, size_t source_len,
                  const std::string &tur_bin,
                  BuildOutputs *out, BuildError *err) {
    if (!out || !err) return false;

    *out = predict_outputs(godot_project_dir, script_path,
                            source_bytes, source_len, tur_bin);
    const std::string &stage         = out->stage_dir;
    const std::string &lib_path      = out->lib_path;
    const std::string &manifest_path = out->manifest_path;
    const std::string  src_dir       = stage + "/src";
    const std::string  build_dir     = stage + "/build";
    const std::string  lib_dir       = build_dir + "/lib";
    const std::string  module        = module_name_for(script_path);
    const std::string &pkg           = out->pkg_name;

    // Fast path -- everything already built. We do not stat the source
    // here because the hash already gates by source bytes.
    if (path_is_file(lib_path) && path_is_file(manifest_path)) {
        out->cache_hit = true;
        return true;
    }

    // Stage the transient project. Layout:
    //   <stage>/build.tur          minimal defpackage
    //   <stage>/src/<module>.tur   verbatim copy of the script source
    if (!mkdir_p(src_dir)) {
        err->message = "turmeric-godot AOT: failed to create cache dir: " + src_dir;
        return false;
    }
    if (!mkdir_p(lib_dir)) {
        err->message = "turmeric-godot AOT: failed to create build/lib dir: " + lib_dir;
        return false;
    }

    if (!write_file(stage + "/build.tur", make_build_tur(pkg))) {
        err->message = "turmeric-godot AOT: failed to write staged build.tur";
        return false;
    }
    const std::string staged_src = src_dir + "/" + module + ".tur";
    if (!write_file_bytes(staged_src, source_bytes, source_len)) {
        err->message = "turmeric-godot AOT: failed to write staged source: " + staged_src;
        return false;
    }

    // Drop a .gitignore so a checked-in .godot dir does not accidentally
    // pull the cache into git. .godot itself is typically gitignored, but
    // belt-and-braces is cheap here.
    const std::string cache_root = cache_root_for(godot_project_dir);
    if (!path_exists(cache_root + "/.gitignore")) {
        (void)write_file(cache_root + "/.gitignore", "*\n");
    }

    // Build command. We invoke through /bin/sh so 2>&1 capture is portable
    // and so a relative `tur` (PATH lookup) resolves correctly.
    //
    // We pin --build-dir to <stage>/build/ so artifacts land at known
    // paths (otherwise build.tur descent picks <stage>/build by default,
    // but pinning it makes the contract explicit).
    std::string log_path = stage + "/build.log";
    std::ostringstream cmd;
    cmd << sh_quote(tur_bin)
        << " build --shared "
        << " --build-dir " << sh_quote(build_dir)
        << " -o "          << sh_quote(lib_path)
        << " --manifest "  << sh_quote(manifest_path)
        << " "             << sh_quote(stage)
        << " > "           << sh_quote(log_path)
        << " 2>&1";

    int rc = std::system(shell_command(cmd.str()).c_str());
    // WEXITSTATUS is a <sys/wait.h> macro that decodes a wait(2) status word.
    // Windows has no such encoding: std::system() hands back the child's exit
    // code as-is, so decoding it there would shift the value and turn a clean
    // exit 1 into something meaningless.
#ifdef _WIN32
    int exit_code = rc;
#else
    int exit_code = (rc == -1) ? -1 : WEXITSTATUS(rc);
#endif
    if (exit_code != 0) {
        // Read the log for diagnostics. Truncate to a sane size so we
        // don't flood the editor's Output panel with megabytes of C
        // diagnostics on a runaway build.
        std::ifstream in(log_path);
        std::stringstream ss;
        ss << in.rdbuf();
        std::string log = ss.str();
        if (log.size() > 16384) log.resize(16384);
        err->exit_code = exit_code;
        err->message = "turmeric-godot AOT: `tur build --shared` failed (exit "
                     + std::to_string(exit_code) + "):\n" + log;
        return false;
    }

    if (!path_is_file(lib_path) || !path_is_file(manifest_path)) {
        err->message = "turmeric-godot AOT: build succeeded but expected "
                       "artifacts are missing (" + lib_path + " or "
                     + manifest_path + ")";
        return false;
    }

    // *out is already populated by predict_outputs; the build just had to
    // produce the artifacts it predicted. cache_hit stays false (we built).
    return true;
}

} // namespace aot
} // namespace godot
