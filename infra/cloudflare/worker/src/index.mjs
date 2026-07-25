// SPDX-License-Identifier: GPL-2.0-or-later
//
// UniLume package router Worker.
// Routes URL paths to R2 objects.
//
// Path structure:
//   /pool/<channel>/deb/<arch>/<filename>       -> binary .deb
//   /pool/<channel>/rpm/<distro>/<release>/<arch>/<filename> -> binary .rpm
//   /pool/<channel>/arch/<arch>/<filename>      -> binary .pkg.tar.zst
//   /pool/<channel>/generic/<arch>/<filename>   -> generic .tar.zst
//   /releases/download/<tag>/<filename>         -> GitHub Release mirror
//   /latest/<channel>/<arch>                    -> redirect to latest version
//   /keys/<filename>                            -> public signing key
//   /status.json                                -> deployment status

const CHANNELS = new Set(['stable', 'testing']);
const VALID_ARCHES = new Set(['amd64', 'x86_64', 'arm64', 'aarch64']);

function sanitizePath(path) {
  // Reject path traversal
  if (path.includes('..')) return null;
  if (path.includes('//')) return null;
  if (path.length > 512) return null;
  return path;
}

function contentType(filename) {
  if (filename.endsWith('.deb')) return 'application/vnd.debian.binary-package';
  if (filename.endsWith('.rpm')) return 'application/x-rpm';
  if (filename.endsWith('.pkg.tar.zst')) return 'application/x-zstd-compressed-tar';
  if (filename.endsWith('.tar.zst')) return 'application/x-zstd-compressed-tar';
  if (filename.endsWith('.asc')) return 'application/pgp-signature';
  if (filename.endsWith('.gpg')) return 'application/pgp-keys';
  if (filename.endsWith('.sig')) return 'application/pgp-signature';
  if (filename.endsWith('.sha256')) return 'text/plain';
  if (filename.endsWith('.json')) return 'application/json';
  if (filename.endsWith('.html')) return 'text/html; charset=utf-8';
  if (filename.endsWith('.css')) return 'text/css; charset=utf-8';
  return 'application/octet-stream';
}

function cacheControl(path, isLatest) {
  if (isLatest) return 'public, max-age=60, must-revalidate';
  if (path.startsWith('/keys/')) return 'public, max-age=86400, immutable';
  if (path.startsWith('/pool/')) return 'public, max-age=31536000, immutable';
  if (path.endsWith('.json')) return 'public, max-age=300, must-revalidate';
  return 'public, max-age=3600';
}

function r2KeyFromPath(url) {
  const path = sanitizePath(url.pathname);
  if (!path) return null;

  // /pool/<channel>/deb/<arch>/<filename>
  const matchPool = path.match(/^\/pool\/(stable|testing)\/(deb|rpm|arch|generic)\/(.+)$/);
  if (matchPool) {
    const [, channel, type, rest] = matchPool;
    return `${channel}/${type}/${rest}`;
  }

  // /releases/download/<tag>/<filename>
  const matchRelease = path.match(/^\/releases\/download\/([^\/]+)\/(.+)$/);
  if (matchRelease) {
    const [, tag, filename] = matchRelease;
    return `releases/${tag}/${filename}`;
  }

  // /keys/<filename>
  const matchKey = path.match(/^\/keys\/(.+)$/);
  if (matchKey) {
    return `keys/${matchKey[1]}`;
  }

  // /status.json
  if (path === '/status.json') {
    return 'status.json';
  }

  return null;
}

async function handleRequest(request, env) {
  const url = new URL(request.url);
  const path = sanitizePath(url.pathname);

  if (!path) {
    return new Response('Bad Request', { status: 400 });
  }

  // Handle /latest/<channel>/<arch> redirects
  const matchLatest = path.match(/^\/latest\/(stable|testing)\/([^\/]+)$/);
  if (matchLatest) {
    const [, channel, arch] = matchLatest;
    if (!CHANNELS.has(channel) || !VALID_ARCHES.has(arch)) {
      return new Response('Not Found', { status: 404 });
    }
    // Read latest version from object metadata or config
    const latestKey = `${channel}/latest-${arch}.txt`;
    try {
      const latestObj = await env.UNILUME_PACKAGES.get(latestKey);
      if (!latestObj) {
        return new Response('Not Found', { status: 404 });
      }
      const version = await latestObj.text();
      return Response.redirect(
        `${url.origin}/pool/${channel}/generic/${arch}/unilume-${version}-linux-${arch}.tar.zst`,
        302
      );
    } catch {
      return new Response('Not Found', { status: 404 });
    }
  }

  // Map path to R2 object key
  const r2Path = r2KeyFromPath(url);
  if (!r2Path) {
    return new Response('Not Found', { status: 404 });
  }

  try {
    const object = await env.UNILUME_PACKAGES.get(r2Path);
    if (!object) {
      return new Response('Not Found', { status: 404 });
    }

    const headers = new Headers();
    headers.set('Content-Type', contentType(r2Path));
    headers.set('Cache-Control', cacheControl(path, false));
    headers.set('Content-Disposition', `attachment; filename="${r2Path.split('/').pop()}"`);
    headers.set('Access-Control-Allow-Origin', '*');

    // Support Range requests
    if (object.httpMetadata?.contentRange) {
      headers.set('Accept-Ranges', 'bytes');
    }

    return new Response(object.body, {
      headers,
      etag: object.etag,
      lastModified: object.uploaded,
    });
  } catch {
    return new Response('Not Found', { status: 404 });
  }
}

export default {
  async fetch(request, env, ctx) {
    try {
      return await handleRequest(request, env);
    } catch (err) {
      return new Response('Internal Server Error', { status: 500 });
    }
  },
};
