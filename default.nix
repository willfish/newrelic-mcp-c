{
  lib,
  stdenv,
  pkg-config,
  python3,
  curl,
  json_c,
}:
stdenv.mkDerivation {
  pname = "newrelic-mcp-c";
  version = "0.1.1";
  src = lib.cleanSource ./.;
  nativeBuildInputs = [
    pkg-config
    python3
  ];
  buildInputs = [
    curl
    json_c
  ];
  makeFlags = [ "PREFIX=$(out)" ];
  meta = {
    description = "Native C stdio MCP server for New Relic";
    homepage = "https://github.com/willfish/newrelic-mcp-c";
    license = lib.licenses.mit;
    mainProgram = "newrelic-mcp-c";
    platforms = lib.platforms.unix;
  };
}
