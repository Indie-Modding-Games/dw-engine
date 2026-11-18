-- The shop job's rules panel (game/shop_job; docs/reference/shop-job.md): the game publishes
-- the pay tiers and the bonus item (shop_job_rules.*, docs/reference/shop-job.md, Interface) and
-- shows this document from the HUD bar's "?", the counter's rules button and by itself at each
-- job's first customer. Confirm, cancel or menu close it (shop_job_rules_close).
--
-- Classes for the skins: #panel takes "improved" with the improved job and "original" with the
-- original's (which shows #rules-original and hides the haggle's rules); #rules-pay-bonus takes
-- "hidden" when the job gives no bonus.

local function setting(name, fallback)
  local value = ui.setting("shop_job_rules." .. name)
  if value == nil then
    return fallback
  end
  return value
end

local function whole(name)
  return math.floor(setting(name, 0))
end

local function refresh(document)
  local improved = setting("improved", false) == true
  local panel = document:get("panel")
  panel:set_class("improved", improved)
  panel:set_class("original", not improved)
  document:get("rules-original"):set_class("hidden", improved)
  document:get("rules-haggle"):set_class("hidden", not improved)
  document:get("rules-characters"):set_class("hidden", not improved)
  document:get("rules-goal"):set_text(t("shop_job.rules.goal", { customers = whole("customers") }))
  document
    :get("rules-pay-lowest")
    :set_text(t("shop_job.rules.pay_lowest", { tally = whole("better_pay") }))
  document
    :get("rules-pay-better")
    :set_text(t("shop_job.rules.pay_better", { tally = whole("better_pay") }))
  document
    :get("rules-pay-joins")
    :set_text(t("shop_job.rules.pay_joins", { tally = whole("joins") }))
  local item = setting("bonus_item", "")
  local bonus = document:get("rules-pay-bonus")
  bonus:set_class("hidden", not improved or item == "" or whole("bonus") <= 0)
  document:get("rules-pay-bonus-text"):set_text(t("shop_job.rules.pay_bonus", {
    tally = whole("bonus"),
    item = item ~= "" and t(item) or "",
  }))
  local icon = document:get("rules-bonus-icon")
  local source = setting("bonus_icon", "")
  icon:set_class("hidden", source == "")
  if source ~= "" then
    icon:set_attribute("src", source)
    icon:set_attribute("rect", setting("bonus_icon_rect", ""))
  end
  document:get("rules-talk"):set_text(t("shop_job.rules.talk", { needs = whole("talk_needs") }))
end

local function refresh_prompts(document)
  document:get("prompt-close"):set_prompt("confirm")
end

function on_show(document)
  refresh(document)
  refresh_prompts(document)
  document:get("rules-close"):focus()
end

function on_setting(document, name)
  if name == "shop_job_rules.revision" then
    refresh(document)
  end
end

function on_language(document)
  refresh(document)
  refresh_prompts(document)
end

function on_device(document)
  refresh_prompts(document)
end

function on_action(_document, action)
  if action == "confirm" or action == "cancel" or action == "menu" then
    ui.send("shop_job_rules_close")
  end
  return true
end
