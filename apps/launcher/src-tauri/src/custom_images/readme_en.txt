Game images to customize
========================

This folder has the images the game read from your disc, ready to edit or
improve. They are for you only: do not share them.

What is here
------------
- map/<map>/background_<variant>.png: the background of each map. The variants are
  day, dusk, night and, on a few maps, paletteN.
  maps.tsv has the name of each map.
- texture/.../<image>_paletteN.png: the images of the title screen, the menus, the
  icons and the numbers of the year and the day. The game draws each image with
  several color palettes; each file is the image with one of them.
  For example, texture/etcna/title2_palette0.png is the title screen and
  texture/etcdat/fi_info_palette12.png has the menu icons and the numbers.

How to improve them
-------------------
1. Edit the images you want. You can keep them at the same size or make them
   2, 3, 4... up to 8 times larger, with the same proportion (for example, from
   768x640 to 1536x1280). The game draws them in the same place and at the same size
   on screen, but with more detail.
2. Do not change the names or the folders. Save as PNG.
3. In the launcher, open Images and load this folder (or a .zip with it). The
   launcher tells you which images it recognizes and which have a name or a size
   that does not work, and it only uses the ones you changed.

On the map backgrounds, the game still uses the original to know which parts
cover the characters, so respect the shapes of the objects.
If an image does not work, the game uses the original and notes it in its log.
