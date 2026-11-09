; a cartridge that names what goes into its ROM
%title "Rock & <Roll> \"Deluxe\""
%version 2.1
%texture TEX_FONT "gfx/font.png"
%texture TEX_BG   "gfx/bg.png"
%texture          "gfx/pre.vtex"            ; used as it is: no conversion
%texture TEX_AGAIN "gfx/bg.png"             ; the same file: the same number
%sound   SND_BOOM "snd/boom.wav"

_main:
  mov R0, TEX_BG
  mov R1, SND_BOOM
  mov R2, TEX_AGAIN
  hlt
