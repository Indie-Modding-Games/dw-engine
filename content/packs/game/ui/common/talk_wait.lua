-- The confirm control a shop's talk box waits on, for a skin that shows it (the speech box's key
-- cap in the bottom right corner). The box holds <div id="talk-wait" class="prompt-icon hidden">;
-- the document's sheet hides it unless a skin draws it (#talk-wait). The document links this script
-- and calls talk_wait.draw(document, waiting) on each refresh: waiting is whether the talk only
-- waits for the player to go on (a greeting, a farewell), not for a choice or a list.

talk_wait = {}

function talk_wait.draw(document, waiting)
  local element = document:get("talk-wait")
  if element == nil then
    return
  end
  if waiting then
    element:set_prompt("confirm", true)
  end
  element:set_class("hidden", not waiting)
end
