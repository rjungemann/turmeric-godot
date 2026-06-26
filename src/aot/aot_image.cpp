// aot_image.cpp -- see aot_image.h.

#include "aot_image.h"

#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <string>

namespace godot {
namespace aot {

namespace {

bool is_ws(char c) { return c == ' ' || c == '\t'; }

void skip_ws(const char *&p, const char *end) {
    while (p < end && is_ws(*p)) p++;
}

// Read a contiguous "non-whitespace, non-special" token. `stops` is a set of
// characters that terminate the token in addition to whitespace. The token
// is returned by reference to keep allocations to one per field.
bool read_token(const char *&p, const char *end, const char *stops,
                std::string *out) {
    skip_ws(p, end);
    const char *start = p;
    while (p < end && !is_ws(*p) && !std::strchr(stops, *p)) p++;
    if (p == start) return false;
    out->assign(start, (size_t)(p - start));
    return true;
}

bool expect_literal(const char *&p, const char *end, const char *lit) {
    skip_ws(p, end);
    size_t n = std::strlen(lit);
    if ((size_t)(end - p) < n) return false;
    if (std::memcmp(p, lit, n) != 0) return false;
    p += n;
    return true;
}

// Split "mod/name" into (mod, name). Returns false when there is no '/'.
bool split_qualified(const std::string &q, std::string *mod, std::string *name) {
    size_t slash = q.find('/');
    if (slash == std::string::npos) return false;
    mod->assign(q, 0, slash);
    name->assign(q, slash + 1, std::string::npos);
    return true;
}

} // namespace

AotType manifest_type_to_aot(const std::string &tag) {
    // emit_module.c::manifest_type_tag enumerates the canonical tags:
    //   :void :bool :int :cstr :ptr :int8..:int64 :uint8..:uint64
    //   :float32 :float64 :never :any
    // Anything else (e.g. user struct types we don't tag yet) is Unknown.
    if (tag == ":void" || tag == ":never") return AotType::Void;
    if (tag == ":float" || tag == ":float32" || tag == ":float64") return AotType::Float;
    if (tag == ":bool") return AotType::Bool;
    if (tag == ":cstr") return AotType::Cstr;
    if (tag == ":ptr")  return AotType::Ptr;
    if (tag == ":int"   || tag == ":int8"  || tag == ":int16" ||
        tag == ":int32" || tag == ":int64" ||
        tag == ":uint8" || tag == ":uint16" ||
        tag == ":uint32"|| tag == ":uint64") return AotType::Int;
    return AotType::Unknown;
}

bool parse_manifest_line(const char *line, size_t len, AotExport *out) {
    if (!line || !out) return false;
    // Trim a trailing newline if present, but parse the rest of the line
    // verbatim -- the emitter never injects CR-LF.
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
    if (len == 0) return false;

    const char *p = line;
    const char *end = line + len;

    // "mod/name"
    std::string qualified;
    if (!read_token(p, end, "", &qualified)) return false;
    if (!split_qualified(qualified, &out->module, &out->name)) return false;

    if (!expect_literal(p, end, "->")) return false;

    // mangled
    if (!read_token(p, end, "", &out->mangled)) return false;

    if (!expect_literal(p, end, "::")) return false;
    if (!expect_literal(p, end, "("))  return false;

    // args (possibly empty) and optional "& :rest"
    out->n_args      = 0;
    out->is_variadic = false;
    out->rest_type   = AotType::Unknown;
    skip_ws(p, end);
    while (p < end && *p != ')') {
        if (*p == '&') {
            p++;
            std::string rest_tag;
            if (!read_token(p, end, ")", &rest_tag)) return false;
            out->is_variadic = true;
            out->rest_type   = manifest_type_to_aot(rest_tag);
            skip_ws(p, end);
            break;
        }
        std::string tag;
        if (!read_token(p, end, ")", &tag)) return false;
        if (out->n_args >= kAotMaxArity) return false;
        out->arg_types[out->n_args++] = manifest_type_to_aot(tag);
        skip_ws(p, end);
    }
    if (!expect_literal(p, end, ")")) return false;
    if (!expect_literal(p, end, "->")) return false;

    std::string ret_tag;
    if (!read_token(p, end, "", &ret_tag)) return false;
    out->ret_type = manifest_type_to_aot(ret_tag);
    return true;
}

std::unique_ptr<AotImage> AotImage::load(const std::string &lib_path,
                                         const std::string &manifest_path,
                                         std::string *out_err) {
    auto die = [&](const std::string &msg) -> std::unique_ptr<AotImage> {
        if (out_err) *out_err = msg;
        return nullptr;
    };

    std::ifstream in(manifest_path);
    if (!in) {
        return die("turmeric-godot AOT: cannot open manifest: " + manifest_path);
    }

    auto image = std::unique_ptr<AotImage>(new AotImage());
    image->lib_path_      = lib_path;
    image->manifest_path_ = manifest_path;

    // Parse the manifest before dlopen so a malformed manifest does not
    // leave a leaked handle behind.
    std::string line;
    size_t lineno = 0;
    while (std::getline(in, line)) {
        lineno++;
        // Skip blank lines / comments. The compiler never emits either
        // today, but tolerating them keeps the format extensible.
        const char *p = line.c_str();
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue;

        AotExport rec;
        if (!parse_manifest_line(line.data(), line.size(), &rec)) {
            return die("turmeric-godot AOT: malformed manifest at line "
                       + std::to_string(lineno) + ": " + line);
        }
        image->exports_.push_back(std::move(rec));
    }

    // dlopen + dlsym each export. RTLD_NOW catches "manifest names a symbol
    // the library doesn't actually export" up front instead of at first
    // dispatch. RTLD_LOCAL keeps per-script symbols isolated -- two scripts
    // exporting `_ready` will not collide.
    image->handle_ = dlopen(lib_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!image->handle_) {
        const char *e = dlerror();
        return die(std::string("turmeric-godot AOT: dlopen failed for ")
                   + lib_path + ": " + (e ? e : "<unknown>"));
    }

    for (auto &rec : image->exports_) {
        // Clear dlerror() so a NULL return is unambiguous -- on some
        // platforms a legit symbol whose value is 0 is technically valid
        // but no AOT lifecycle entry would ever be that.
        dlerror();
        void *sym = dlsym(image->handle_, rec.mangled.c_str());
        const char *e = dlerror();
        if (e || !sym) {
            std::string msg = "turmeric-godot AOT: missing symbol '"
                            + rec.mangled + "' in " + lib_path;
            if (e) { msg += ": "; msg += e; }
            return die(msg);
        }
        rec.fn_ptr = sym;
    }

    return image;
}

AotImage::~AotImage() {
    if (handle_) {
        dlclose(handle_);
        handle_ = nullptr;
    }
}

const AotExport *AotImage::find(const char *module, const char *name) const {
    if (!name) return nullptr;
    const char *want_mod = (module && *module) ? module : "_";
    for (const auto &e : exports_) {
        if (e.module == want_mod && e.name == name) return &e;
    }
    return nullptr;
}

const AotExport *AotImage::find_by_name(const char *name) const {
    if (!name) return nullptr;
    for (const auto &e : exports_) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

} // namespace aot
} // namespace godot
