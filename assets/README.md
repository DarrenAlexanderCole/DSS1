# Assets

- `fonts/`: Archivo Narrow (Bold and Medium, static instances of the variable font), Michroma and VT323, all
  from Google Fonts under the SIL Open Font License 1.1 (`OFL-*.txt`). They are compiled into the app.
- `textures/`: worn-panel textures made for DSS1 (generated, no third-party images): `panel-wear` (grain,
  scratches, scuffs, dust), `grime`, `edge-wear` (paint rubbed off at the edges), `aluminium` (brushed rack
  ears) and `ink-wear` (patchy silk-screen ink). Tileable except `edge-wear`, which is stretched.


## render/

Rendered parts and generated textures for the hi-fi look (2026-11-19), all made here, no third-party art:
knobs, black phenolic keys, toggles, screws, the power lamp and rawhide pads rendered with Blender by `tools/render/render_parts.py`; brushed
aluminium, burl walnut, rawhide, scratch and wear maps made by `tools/render/make_textures.py`. Re-run those scripts to change them.
