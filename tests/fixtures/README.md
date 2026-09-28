# Character mapping references

These fixtures contain identity and class metadata, with no models or animation
data. They are the source references for `cod4_character_translation.cpp`.

- `cod4-mp-characters.tsv`: 34 `raw/character/character_mp_*.gsc` scripts from
  [COD4 Mod Tools at 95c76d6](https://github.com/cod4mw/CoD4-Mod-Tools/tree/95c76d6a470bea43fdd5ab591afe8475fac7699d/raw/character).
  Role/faction selection follows `raw/maps/mp/gametypes/_teams.gsc` and the
  corresponding `raw/mptype` scripts. The four unused legacy USMC CQB,
  engineer, grenadier and rifleman scripts retain compatibility mappings to
  their corresponding MWR class roles.
- `cod4-map-environments.tsv`: `axis_soldiertype` values from the same pinned
  reference's `raw/maps/mp/mp_*.gsc` scripts. These disambiguate reused Russian
  model names; they are not a map allowlist.
- `mwr-stock-characters.tsv`: default male class bodies, heads and viewhands
  selected by the installed MWR engine's `mp/costumemodeltable.csv`, captured
  on 2026-09-28. Selection uses category, class, environment and ally groups.
  Each target was checked against the running H1 resident/transient catalog.

The MWR table splits heads from bodies where some original COD4 models contain
an integrated head. It also defines distinct SAS and Spetsnaz ghillie hands for
the single original `viewhands_marine_sniper` identity. Test expectations use
the recorded body context to resolve those relationships.
