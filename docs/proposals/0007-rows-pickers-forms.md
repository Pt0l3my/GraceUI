# Proposal 0007: Row actions, live pickers, row secrets and small forms

**Status: proposal, 2026-11-21, put to the owner and EldenNet's session (G34); not built.** EldenNet's session met these
building its AI pages (Providers, Models, Self-test; its WHY§274 to §276) on GraceUI 0.2.7, and each has a stand-in
today. They add to the page data and to what GraceUI's views draw, for every mod (G16, G18), so they are put here
rather than settled alone. None changes the ABI. A bare § is a section of [DESIGN.md](../DESIGN.md).

## 1. An action on each row of a table

**Today** a page's actions are in its header, so EldenNet has one "Remove unused providers" where each provider's
card wants its own Remove. **Proposed:** a table column with an `action`, as D181's switch column: `{"id": "remove",
"label": "", "action": {"command": "providers.remove", "key": "id", "label": "Remove", "tone": "destructive",
"confirm": "The provider and its key are removed."}}`. Each row draws a button; pressing it sends the command with
`{key: row[key]}`, a destructive one only after its second press and with `"confirm": true`, as D173 and D183 hold
for header actions: the registry refuses a destructive row action without its `confirm` text, and the server a
destructive command without `"confirm": true`. In game the row's button takes focus after its switch, if any (→ from
the row, §5.4). *Recommended.*

## 2. A picker whose choices come from a topic

**Today** a choice setting's choices are fixed in the settings description, which GraceUI's store checks every value
against; so EldenNet's model for each slot comes only through a preset, though the provider's `/v1/models` lists them.
**Options:** (a) **`choices_from`** on a string setting: the name of a state topic whose body is `{"choices": [{"value",
"label"}]}`; both views draw a picker from it (a list in the browser, ← and → over it in game), "Nothing to choose
from yet" while it is empty, and keep the value shown even when it is not in the list; GraceUI's store checks the type
and length (1 to 200 bytes) and the space checks membership at its one entry point (invariant 3); (b) a free text
field with suggestions; (c) nothing: presets only. *Recommended:* (a). The value stays a setting (one home, one entry
point, kept with the space's settings); only its choices are live.

## 3. A secret for each row

**Today** a secret is a setting, so a key exists only under a preset provider's name, and a custom provider's key stays
in the player's own file. **Proposed:** a table column with a `secret`: `{"id": "key", "label": "Key", "secret":
{"command": "providers.key", "key": "id"}}`. Each row draws a masked field (in the browser; in game "Edited in the
browser view" until GraceUI's own text field, §5.6) and its status from the row's cell (`"set"` or `""`, shown as Set
or Missing). Saving sends `{key: row[key], "value": "<the secret>"}`, and an empty value clears it. GraceUI holds the
command to invariant 5 as it does a secret setting: the browser clears the field after sending and keeps it in no
storage; the server and the registry never log or echo the command's `value`, and its result carries no body; the
space must publish the cell's status only (D183's mask applies to the `settings` topic, and the lint and the guide say
the same for these cells). The command is marked sensitive for pairing (§4 below). *Recommended* over a `secrets.set`
with a provider argument from a loose field, which would need its own place on the page and its own status.

## 4. A small form

**Today** EldenNet cannot ask for a custom provider's name and base URL. **Proposed:** a `form` block: `{"kind":
"form", "command": "providers.add", "fields": [{"id": "name", "label": "Name", "max": 32}, {"id": "base_url", "label":
"Base URL", "max": 200}], "submit": "Add"}`, text fields only at first (1 to 4 of them, each 1 to 200 bytes), sent as
`{field: value}` to the space, which validates them and answers in words; a refusal shows under the form, as under a
row. In game a form says "Filled in the browser view" until GraceUI's own text field (§5.6). *Recommended.*

## 5. Which view sent a command (the security review's open item)

A provider's key, its row secret, Add and Remove should be refused from a paired device once pairing exists (§7.2 item
16, G9). That needs D183's open item, telling a space which view sent a command, and a way for a space to mark its
sensitive commands (§6.6's last line). It belongs with pairing, not here: until pairing, both views are this PC's.
This proposal only marks items 1, 3 and 4's commands as candidates for that list.

## 6. Cost, and order

1, 3 and 4: the page data's types in `protocol.toml`, the page lint and registration checks, both views, and the
fixtures and goldens; 3 also the server's and registry's handling of the `value` they never log. 2: the settings
description's `choices_from`, the store's check, both views. No change of the ABI. EldenNet listed them in this order, with
no hurry: each has a stand-in today.

## 7. For the owner and EldenNet's session

Which to build, and for 2, which option.
