-- The sleep menu, once the partner slept (docs/architecture/guardar-y-cargar.md, 3.6): sleep on, or
-- save to the playthrough's slot first. It sends sleep_only or sleep_and_save. Like the original,
-- it cannot be cancelled: the night has already passed.

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
end

function on_show(document)
  refresh_prompts(document)
  document:get("sleep"):focus()
end

function on_device(document)
  refresh_prompts(document)
end

function on_action(_document, action)
  return action == "cancel" or action == "menu"
end
