# Additional assets

Install these in your `./DATA` directory or copy them into the carmageddon 1 data directory. They are for new features added by this pack.

Optional. If they are not installed they just wont show.

FWIW the `ANIM` files below are not AI generated and were created by hand. The
widescreen cockpits are a separate case and are described further down.

- `ANIM/ANNIEVIEW.FLI` - Name for Die Anna used on the opponent screen if you enable `AddOtherPlayerCharacterAsOpponent`
- `ANIM/FRANKVIEW.FLI` - Name for Max Damage used on the opponent screen if you enable `AddOtherPlayerCharacterAsOpponent`
- `ANIM/COLICIUM.FLI` - Race card screenshot used on the main menu after selecting COLICIUM race when `MeldNetRaces` is enabled
- `ANIM/SUMOBIG.FLI - Race card screenshot used on the main menu after selecting SUMO race when `MeldNetRaces` is enabled
- `ANIM/MAINACHFL.FLI` - Achievement menu option (not selected)
- `ANIM/MAINACHGL.FLI` - Achievement menu option (selected)

## Widescreen cockpits

- `11X48X8/PIXELMAP/CKPT*.PIX` - the original 4:3 cockpits widened to 21:9, so the game fills an ultrawide screen instead of falling back to a letterboxed 4:3 cockpit. A 16:9 screen loads the same files and simply sees less of them, at the same pixel scale; there is no separate 16:9 set and nothing should be installed into `85X48X8`.
- `COCKPIT.TXT` - rear-view mirror rects for those cockpits. Where the widened art continues a mirror past the edge the 4:3 frame cut it off at, the painted glass is wider than the rect in the car's own TXT. A cockpit with no entry here keeps its car TXT rect, as does every cockpit if this file is absent.

Unlike the `ANIM` files above, **these are AI generated** - though only in part. The original 4:3 artwork is centred in the wider canvas and left completely untouched; only the two 240px strips either side of it are masked and filled in, by Flux Fill dev running locally, composing from the surrounding image. So every pixel the original game drew is still the original game's; what is generated is the invented rest of the cabin to the left and right of it.

Derived from the original game art, which is why this is our own work rather than a copy of someone else's widescreen pack.

## Also ships: 
- `PIXELMAP/SMOKE.PIX` - This is a game asset and is provided in the splat pack demo and xmas demo so is ok to distribute. It is needed if you want to play a version of the game that did not ship with 3dfx assets e.g. original Carmageddon CD.
