/** @type {import('next').NextConfig} */
const nextConfig = {
  reactStrictMode: true,

  // Static export → Firebase Hosting (no SSR / no functions needed).
  // All pages in this app are client-rendered ("use client") so this is safe.
  output: "export",

  // We use <img> tags (not next/image) for survivor photos which are
  // already base64 dataURLs. Unoptimizing keeps the static build pure HTML.
  images: { unoptimized: true },

  // Firebase Hosting serves /foo/index.html for /foo when trailingSlash=true.
  // Without it, /register would 404. Required for `output: export`.
  trailingSlash: true,
};

export default nextConfig;
