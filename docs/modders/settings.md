# The settings description

A space's settings are described once, as data, and GraceUI draws every row from that description in both views
(§6.9, invariant 12). A mod writes no settings page of its own. It places a setting's rows with `settings` blocks on
its home page, and `shortcuts` blocks elsewhere. It owns the values: GraceUI's views send `settings.set` and
`settings.reset` to the space's one entry point, which checks and keeps them.

In `space.toml` each setting is a `[[setting]]` table.

## A setting

| Field | Meaning |
|---|---|
| `key` | Its id, unique in the space |
| `type` | `bool`, `integer`, `number`, `duration_s`, `choice`, `tri_state`, `key_name`, `text` or `secret` |
| `default` | Its default, of its type and within its bounds |
| `min`, `max`, `step` | A number's bounds and step, each of at most four decimals; numbers alone have them |
| `unit` | A number's unit after its value, at most 16 characters ("s", "%") |
| `choices` | A choice's `{value, label}`, two or more, its default one of them |
| `choices_from` | A text setting's picker: a state topic of the space's whose body is `{choices: [{value, label}]}` (1.2) |
| `third` | A tri-state's third value: `auto` (the space decides at the moment) or `inherit` (taken from a chain the space resolves) |
| `label`, `help` | Its row's label (at most 128) and its help, which ends with the default as the view words it |
| `synonyms` | Other words search finds it by |
| `home` | The one page whose `settings` block shows it |
| `effect` | When a change takes hold: `now`, `next_moment`, `next_request` or `next_launch`, shown as a faint word |
| `availability` | `live`; `later` (locked, "Later version"); `inert` ("No effect in this version"); `check:<id>` ("Waits on a game check"); `version:<x.y>` ("Takes effect in vX.Y"); `requires:<key>` ("Off because … is off") |
| `locked` | It does not change in this version |
| `sensitive` | It is set or reset from this PC alone, never from a paired device (1.8) |

A secret's default is empty, never a value: a secret is write-only, and every view sees only whether one is set.

## The settings topic

GraceUI serves the space's `settings` topic from what the space publishes:

| Member | Meaning |
|---|---|
| `schema` | The description's number and a hash of it |
| `description` | Every setting, as above |
| `values` | Each key's value; a key not in it shows its default |
| `changed` | The keys changed from their defaults, which carry the row's lozenge and its reset (D198) |
| `inherited` | What each tri-state left on `inherit` resolves to now: `{key, on, source, page}`, drawn faint beside it (D163) |

## The commands

| Command | Arguments | The space checks |
|---|---|---|
| `settings.set` | `key`, `value` | The key is its own, the value of its type and in its bounds, the setting not locked |
| `settings.reset` | `scope` (`key`, `page` or `all`) and its `target`; `confirm` for a page or all | A page or all needs `confirm: true` |

GraceUI refuses a sensitive setting's change from a paired device itself, before the space hears of it (1.8). A refusal
names its reason in the space's own words (`detail`), and the view puts them under the row and returns focus to it
(§5.7).
