#include "propfind.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

// Finds the next tag whose local name (after an optional "prefix:") equals
// `local`. `closing` selects "</x>" vs "<x ...>" / "<x/>".
// Returns pointer to '<' or NULL. *self_closing is set for opening tags.
static const char *find_tag(const char *p, const char *end, const char *local, bool closing,
                            bool *self_closing, const char **after) {
    size_t llen = strlen(local);
    while (p < end) {
        const char *lt = memchr(p, '<', (size_t)(end - p));
        if (!lt) return NULL;
        const char *q = lt + 1;
        bool is_close = (*q == '/');
        if (is_close) q++;
        if (*q == '?' || *q == '!') {
            p = q;
            continue;
        }
        const char *gt = memchr(q, '>', (size_t)(end - q));
        if (!gt) return NULL;
        // Tag name spans until whitespace, '/' or '>'.
        const char *ne = q;
        while (ne < gt && *ne != ' ' && *ne != '\t' && *ne != '\r' && *ne != '\n' && *ne != '/') ne++;
        const char *colon = memchr(q, ':', (size_t)(ne - q));
        const char *ln = colon ? colon + 1 : q;
        if (is_close == closing && (size_t)(ne - ln) == llen && strncmp(ln, local, llen) == 0) {
            if (self_closing) *self_closing = (gt > q && gt[-1] == '/');
            if (after) *after = gt + 1;
            return lt;
        }
        p = gt + 1;
    }
    return NULL;
}

// Extracts trimmed text content of the first <local> element within [p, end).
static bool tag_text(const char *p, const char *end, const char *local, char *dst, size_t dst_size) {
    bool self = false;
    const char *after;
    if (!find_tag(p, end, local, false, &self, &after)) return false;
    if (self) {
        dst[0] = 0;
        return true;
    }
    const char *close = find_tag(after, end, local, true, NULL, NULL);
    if (!close) return false;
    while (after < close && (*after == ' ' || *after == '\n' || *after == '\r' || *after == '\t')) after++;
    const char *e = close;
    while (e > after && (e[-1] == ' ' || e[-1] == '\n' || e[-1] == '\r' || e[-1] == '\t')) e--;
    size_t n = (size_t)(e - after);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, after, n);
    dst[n] = 0;
    return true;
}

// Decodes the few XML entities that can appear in hrefs.
static void xml_unescape(char *s) {
    static const struct { const char *ent; char c; } ents[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
    char *o = s;
    for (char *p = s; *p;) {
        bool done = false;
        if (*p == '&') {
            for (size_t i = 0; i < sizeof ents / sizeof ents[0]; i++) {
                size_t n = strlen(ents[i].ent);
                if (strncmp(p, ents[i].ent, n) == 0) {
                    *o++ = ents[i].c;
                    p += n;
                    done = true;
                    break;
                }
            }
        }
        if (!done) *o++ = *p++;
    }
    *o = 0;
}

bool propfind_parse(const char *xml, DavList *out) {
    out->items = NULL;
    out->count = 0;
    size_t cap = 0;
    const char *p = xml;
    const char *end = xml + strlen(xml);
    bool found = false;
    for (;;) {
        bool self;
        const char *body;
        if (!find_tag(p, end, "response", false, &self, &body)) break;
        const char *close = find_tag(body, end, "response", true, NULL, NULL);
        if (!close) break;
        p = close + 1;
        found = true;

        char href[1024];
        if (!tag_text(body, close, "href", href, sizeof href)) continue;
        xml_unescape(href);
        // Strip scheme://host if present.
        char *path = href;
        char *scheme = strstr(href, "://");
        if (scheme) {
            char *slash = strchr(scheme + 3, '/');
            path = slash ? slash : (char *)"/";
        }
        url_decode_inplace(path);

        DavEntry e;
        memset(&e, 0, sizeof e);
        e.size = -1;

        size_t n = strlen(path);
        bool trailing = n > 0 && path[n - 1] == '/';
        while (n > 0 && path[n - 1] == '/') path[--n] = 0;
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        str_copy(e.name, sizeof e.name, base);

        // A collection has <resourcetype><collection/></resourcetype>.
        const char *rt_after;
        bool rt_self = false;
        if (find_tag(body, close, "resourcetype", false, &rt_self, &rt_after) && !rt_self) {
            const char *rt_close = find_tag(rt_after, close, "resourcetype", true, NULL, NULL);
            if (rt_close && find_tag(rt_after, rt_close, "collection", false, NULL, NULL)) e.is_dir = true;
        }
        if (trailing) e.is_dir = true;

        char tmp[128];
        if (tag_text(body, close, "getlastmodified", tmp, sizeof tmp)) {
            time_t t;
            if (parse_http_date(tmp, &t)) e.modified = t;
        }
        if (tag_text(body, close, "getcontentlength", tmp, sizeof tmp) && tmp[0]) e.size = strtoll(tmp, NULL, 10);

        if (out->count == cap) {
            size_t ncap = cap ? cap * 2 : 16;
            DavEntry *ni = realloc(out->items, ncap * sizeof *ni);
            if (!ni) {
                davlist_free(out);
                return false;
            }
            out->items = ni;
            cap = ncap;
        }
        out->items[out->count++] = e;
    }
    return found;
}

void davlist_free(DavList *l) {
    free(l->items);
    l->items = NULL;
    l->count = 0;
}

const DavEntry *davlist_newest(const DavList *l, const char *prefix, const char *suffix) {
    const DavEntry *best = NULL;
    for (size_t i = 0; i < l->count; i++) {
        const DavEntry *e = &l->items[i];
        if (e->is_dir || !str_starts_with(e->name, prefix) || !str_ends_with(e->name, suffix)) continue;
        if (!best || e->modified > best->modified ||
            (e->modified == best->modified && strcmp(e->name, best->name) > 0))
            best = e;
    }
    return best;
}
