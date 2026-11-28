-- Shown once a run, without input, when the game draws on the processor or in the compatible
-- mode (docs/architecture/software-mode.md, docs/architecture/graphics-diagnosis.md).

local values = { "gpu", "adapter", "dedicated", "driver", "minimum", "api" }
local cards = { gpu = true, adapter = true, dedicated = true }

local reasons = {
  ["basic-display-adapter"] = true,
  ["dedicated-gpu-unused"] = true,
  ["driver-too-old"] = true,
  ["no-vendor-driver"] = true,
  ["vulkan-too-old"] = true,
  ["missing-features"] = true,
  ["gpu-unsuitable"] = true,
  ["hardware-no-vulkan"] = true,
  ["instance-failed"] = true,
  ["device-failed"] = true,
  ["device-lost"] = true,
  ["out-of-memory"] = true,
  ["virtual-machine"] = true,
  ["remote-session"] = true,
  ["implicit-layers"] = true,
  ["compatible-path"] = true,
  ["no-gpu"] = true,
}

local function refresh(document)
  local prefix = ui.setting("software_notice.kind") == "compatible"
      and "software_notice.compatible."
    or "software_notice."
  document:get("title"):set_text(t(prefix .. "title"))
  document:get("body"):set_text(t(prefix .. "body"))
  local code = ui.setting("software_notice.reason") or ""
  if reasons[code] then
    local args = {}
    for _, name in ipairs(values) do
      local value = ui.setting("software_notice." .. name) or ""
      if value == "" and cards[name] then
        value = t("software_notice.your_card")
      end
      args[name] = value
    end
    document:get("fix"):set_text(t("software_notice.reason." .. code:gsub("-", "_"), args))
  else
    document:get("fix"):set_text(t(prefix .. "fix"))
  end
end

function on_show(document)
  refresh(document)
end

function on_language(document)
  refresh(document)
end
