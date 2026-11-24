-- The notice that the game draws without a graphics card (docs/architecture/software-mode.md):
-- the game shows it once a run in software mode (the software_mode setting) and hides it after a
-- few seconds; it takes no input.

local function refresh(document)
  document:get("title"):set_text(t("software_notice.title"))
  document:get("body"):set_text(t("software_notice.body"))
  document:get("fix"):set_text(t("software_notice.fix"))
end

function on_show(document)
  refresh(document)
end

function on_language(document)
  refresh(document)
end
