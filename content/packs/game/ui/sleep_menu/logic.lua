-- The sleep menu, once the partner slept (docs/architecture/save-and-load.md, 3.6): sleep on, or
-- save to the playthrough's slot first. It sends sleep_only or sleep_and_save. Like the original,
-- it cannot be cancelled: the night has already passed. A co-op guest asleep ahead of the world
-- sees the wait for the world's morning instead (sleep_menu.waiting, sleep_menu.wait_seconds),
-- which menu or cancel end with sleep_wake (docs/architecture/coop-training.md, 4.5).

local function waiting()
  return ui.setting("sleep_menu.waiting") == true
end

local function refresh_prompts(document)
  document:get("prompt-confirm"):set_prompt("confirm")
  document:get("prompt-wake"):set_prompt("menu")
end

local function draw_wait(document)
  local wait = waiting()
  for _, id in ipairs({ "sleep", "save-and-sleep", "prompt-accept" }) do
    document:get(id):set_class("hidden", wait)
  end
  document:get("wait"):set_class("hidden", not wait)
  document:get("prompt-wake-box"):set_class("hidden", not wait)
  if not wait then
    return
  end
  local seconds = ui.setting("sleep_menu.wait_seconds")
  local left
  if type(seconds) == "number" and seconds >= 0 then
    local whole = math.floor(seconds)
    left = t("online.coop.sleep_left", { time = string.format("%d:%02d", whole // 60, whole % 60) })
  else
    left = t("online.coop.sleep_wait")
  end
  document:get("wait-left"):set_text(left)
end

function on_show(document)
  refresh_prompts(document)
  draw_wait(document)
  if not waiting() then
    document:get("sleep"):focus()
  end
end

function on_setting(document, name)
  if name == "sleep_menu.waiting" or name == "sleep_menu.wait_seconds" then
    draw_wait(document)
  end
end

function on_device(document)
  refresh_prompts(document)
end

function on_action(_document, action)
  if waiting() then
    if action == "cancel" or action == "menu" then
      ui.send("sleep_wake")
    end
    return true
  end
  return action == "cancel" or action == "menu"
end
