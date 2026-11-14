-- a70q: show the Sxmo on-screen keyboard (wvkbd) when a text field gets focus, hide it when the
-- focus leaves. fcitx5 receives these as input context FocusIn/FocusOut (Wayland text-input-v3
-- enable/disable via sway's input-method-v2). A FocusOut is acted on after 0.4 s unless another
-- FocusIn arrives (moving between fields must not make the keyboard flicker).
local fcitx = require("fcitx")
local seqfile = (os.getenv("XDG_RUNTIME_DIR") or "/tmp") .. "/a70q-osk.seq"
local seq = 0

local function bump()
    seq = seq + 1
    local f = io.open(seqfile, "w")
    if f then f:write(tostring(seq)); f:close() end
    return seq
end

function a70qosk_focus_in()
    bump()
    os.execute("sxmo_keyboard.sh open >/dev/null 2>&1 &")
end

function a70qosk_focus_out()
    local s = bump()
    os.execute(string.format(
        "(sleep 0.4; [ \"$(cat %s 2>/dev/null)\" = %d ] && sxmo_keyboard.sh close) >/dev/null 2>&1 &",
        seqfile, s))
end

fcitx.watchEvent(fcitx.EventType.FocusIn, "a70qosk_focus_in")
fcitx.watchEvent(fcitx.EventType.FocusOut, "a70qosk_focus_out")
