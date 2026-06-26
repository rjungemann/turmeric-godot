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
#include <sys/wait.h>
#include <unistd.h>

namespace godot {
namespace aot {

namespace {

// --- mkdir -p ---------------------------------------------------------------

bool mkdir_p(const std::string &path) {
    if (path.empty()) return false;
    std::string acc;
    acc.reserve(path.size());
    for (size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') {
            if (!acc.empty() && acc != "/") {
                if (mkdir(acc.c_str(), 0755) != 0 && errno != EEXIST) {
                    return false;
                }
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
    if (realpath(tur_bin.c_str(), real)) {
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

// Shell-quote a path for /bin/sh consumption. We invoke the compiler via a
// shell so stdout/stderr capture composes; we never embed user-supplied
// strings without quoting.
std::string sh_quote(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') out.append("'\\''");
        else            out.push_back(c);
    }
    out.push_back('\'');
    return out;
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
        const char *tmp = std::getenv("TMPDIR");
        if (!tmp || !*tmp) tmp = "/tmp";
        return std::string(tmp) + "/turmeric-godot-cache";
    }
    return godot_project_dir + "/.godot/turmeric-cache";
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
    const std::string lib_path = stage + "/build/lib/lib" + pkg + ".so";
    o.stage_dir     = stage;
    o.lib_path      = lib_path;
    o.manifest_path = lib_path + ".manifest";
    o.metadata_path = stage + "/exports.metadata";
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
    // pkg name is reconstructed from the hash embedded in lib_path; keep
    // the staged build.tur naming consistent with predict_outputs.
    const std::string  pkg = [&]() {
        const std::string prefix = "/lib";
        const std::string suffix = ".so";
        auto pos = lib_path.rfind(prefix);
        auto epos = lib_path.rfind(suffix);
        if (pos == std::string::npos || epos == std::string::npos) return std::string();
        return lib_path.substr(pos + prefix.size(), epos - (pos + prefix.size()));
    }();

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

    int rc = std::system(cmd.str().c_str());
    int exit_code = (rc == -1) ? -1 : WEXITSTATUS(rc);
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
