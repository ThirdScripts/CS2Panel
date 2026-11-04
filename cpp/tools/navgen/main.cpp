// NavMesh generator: turns a walkable raster into a mesh asset.
//
// The Dust II mesh shipped with this project was produced by this tool from a
// radar frame captured on the real map. The raster marks floor space as
// mid-grey while walls and props stay dark or bright, matching how the CS2
// radar paints the world.
//
// Usage:
//   cs2n_navgen --input walkable.pgm --output ../assets/dust2.nav.json
//               [--units 1.0] [--block 8] [--fill 0.6] [--name de_dust2]
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "navigation/navmesh_builder.h"
#include "navigation/navmesh_io.h"
#include "vision/image.h"

using namespace cs2n;

int main(int argc, char** argv) {
  std::string input;
  std::string output;
  NavMeshBuilderConfig config;
  config.mapName = "de_dust2";
  config.unitsPerPixel = 1.0;
  config.blockPixels = 8;
  config.blockFillRatio = 0.6;

  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--input" && i + 1 < argc) {
      input = argv[++i];
    } else if (argument == "--output" && i + 1 < argc) {
      output = argv[++i];
    } else if (argument == "--units" && i + 1 < argc) {
      config.unitsPerPixel = std::atof(argv[++i]);
    } else if (argument == "--block" && i + 1 < argc) {
      config.blockPixels = std::atoi(argv[++i]);
    } else if (argument == "--fill" && i + 1 < argc) {
      config.blockFillRatio = std::atof(argv[++i]);
    } else if (argument == "--name" && i + 1 < argc) {
      config.mapName = argv[++i];
    }
  }

  if (input.empty() || output.empty()) {
    std::printf("usage: cs2n_navgen --input walkable.pgm --output mesh.json [--units U] [--block N] [--fill R] [--name S]\n");
    return 2;
  }

  std::vector<std::uint8_t> gray;
  int width = 0;
  int height = 0;
  std::string error;
  if (!Image::loadGrayPgm(input, gray, width, height, error)) {
    std::printf("cannot read %s: %s\n", input.c_str(), error.c_str());
    return 1;
  }

  const RasterImage raster = NavMeshBuilder::fromLuminance(gray, width, height, 32, 125);
  NavMesh mesh;
  if (!NavMeshBuilder::build(raster, config, mesh, error)) {
    std::printf("build failed: %s\n", error.c_str());
    return 1;
  }

  if (!NavMeshIo::saveFile(mesh, output, error)) {
    std::printf("cannot write %s: %s\n", output.c_str(), error.c_str());
    return 1;
  }

  std::printf("wrote %s\n", output.c_str());
  std::printf("  raster       : %dx%d\n", width, height);
  std::printf("  areas        : %zu\n", mesh.areaCount());
  std::printf("  connections  : %zu\n", mesh.connectionCount());
  std::printf("  bounds       : (%.0f,%.0f)..(%.0f,%.0f)\n", mesh.boundsMin().x, mesh.boundsMin().y,
              mesh.boundsMax().x, mesh.boundsMax().y);
  return 0;
}
