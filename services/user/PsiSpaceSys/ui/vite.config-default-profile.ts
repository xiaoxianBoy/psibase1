import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import path from "path";
import alias from "@rollup/plugin-alias";
import svgr from "vite-plugin-svgr";

const psibase = (appletContract: string, isServing?: boolean) => {
    const buildAliases = [
        {
            find: "/common/iframeResizer.contentWindow.js",
            replacement: path.resolve(
                "../../CommonSys/common/thirdParty/src/iframeResizer.contentWindow.js"
            ),
        },
        {
            // bundle non-external (above) common files except fonts (which should only be referenced)
            find: /^\/common(?!\/(?:fonts))(.*)$/,
            replacement: path.resolve("../../CommonSys/common/resources$1"),
        },
    ];

    if (isServing) {
        buildAliases.push({
            find: /^@psibase\/common-lib.*$/,
            replacement: path.resolve(
                "../../CommonSys/common/packages/common-lib/src"
            ),
        });
    }

    return [
        {
            name: "psibase",
            config: () => {
                return {
                    build: {
                        assetsDir: "",
                        cssCodeSplit: false,
                        rollupOptions: {
                            external: [
                                "/common/rootdomain.mjs",
                                "/common/iframeResizer.js",
                                "/common/common-lib.js",
                            ],
                            makeAbsoluteExternalsRelative: false,
                            output: {
                                entryFileNames: "index.js",
                                assetFileNames: "[name][extname]",
                            },
                        },
                    },
                    server: {
                        host: "psibase.127.0.0.1.sslip.io",
                        port: 8081,
                        proxy: {
                            "/": {
                                target: "http://psibase.127.0.0.1.sslip.io:8079/",
                                bypass: (req, _res, _opt) => {
                                    const host = req.headers.host || "";
                                    const subdomain = host.split(".")[0];
                                    if (
                                        subdomain === appletContract &&
                                        req.method !== "POST" &&
                                        req.headers.accept !==
                                            "application/json" &&
                                        !req.url.startsWith("/common") &&
                                        !req.url.startsWith("/api")
                                    ) {
                                        return req.url;
                                    }
                                },
                            },
                        },
                    },
                    resolve: {
                        alias: buildAliases,
                    },
                };
            },
        },
        alias({
            entries: [
                {
                    find: /^@psibase\/common-lib.*$/,
                    replacement: "/common/common-lib.js",
                },
            ],
        }),
    ];
};

const root = path.resolve(__dirname, "src");

// https://vitejs.dev/config/
export default defineConfig({
    root,
    base: "/default-profile/",
    plugins: [
        react(),
        svgr({ exportAsDefault: true }),
        psibase("psispace-sys"),
    ],
    build: {
        outDir: "../dist/default-profile",
        emptyOutDir: true,
        rollupOptions: {
            input: {
                main: path.resolve(root, "default-profile.html"),
            },
        },
    },
});
