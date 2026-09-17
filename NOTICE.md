# Third-party content and licensing

This repository mixes two licences. Please read this before redistributing it
or using it commercially.

## The code — MIT

Everything under `src/`, `tools/`, `web/`, `CMakeLists.txt` and `CMakePresets.json` is
original work under the MIT licence (see `LICENSE`).

## The machine's shape — CC BY-NC-SA 4.0

The simulated machine is **Maxwell Boltzmann Kinetic Galton Board** by
**gao.lab**:

- <https://makerworld.com/en/models/2926630-maxwell-boltzmann-kinetic-galton-board>
- Licensed **[CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/)**
  (Attribution — NonCommercial — ShareAlike)

### What that means here

- **`MaxwellBoltzmann775.3mf` is not redistributed.** The printable model is
  gao.lab's file; download it yourself from the link above. `.gitignore`
  excludes it.
- **`assets/geometry.json` is a derivative work of that model.** It holds the
  cross-section outline and the measured dimensions that `tools/` extract from
  the 3MF, so it inherits the model's licence:
  **CC BY-NC-SA 4.0, © gao.lab**, adaptation by this project.
- Because the shape is embedded into the executable through `assets/assets.qrc`,
  **a binary built from this repository also carries the NonCommercial and
  ShareAlike terms**. Do not ship it commercially.
- `docs/*.png` and `docs/demo.gif` show the derived shape and are covered by the
  same CC BY-NC-SA 4.0 terms.

If you want a build with no third-party terms attached, delete
`assets/geometry.json`, point the tooling at your own geometry, and drop
`assets/assets.qrc` from the build.

## Physics description

The explanation of how the machine works, quoted and paraphrased in `README.md`,
comes from gao.lab's model description on the page linked above.

## Browser distribution

The HTML5/WebAssembly version uses the same derived geometry. Its published
`geometry.json` and machine visualisation retain the CC BY-NC-SA 4.0 terms
above. The WebAssembly physics code and original web interface are MIT.
