local fuel_gauge = require("lib_fuel_gauge")
local delay = require("delay")
local i2c = require("i2c")

local a = type(args) == "table" and args or {}
local function int_arg(k, default)
    local v = a[k]
    if type(v) == "number" then
        return math.floor(v)
    end
    return default
end

local CHIP = type(a.chip) == "string" and a.chip or nil
local I2C_ADDR = int_arg("addr", nil)
local FREQ_HZ = int_arg("freq_hz", 400000)
local SAMPLE_COUNT = int_arg("samples", 20)
local INTERVAL_MS = int_arg("interval_ms", 1000)

local gauge
local bus

local function current_state_text(current_ma)
    if type(current_ma) ~= "number" then
        return nil
    end
    -- BQ27220 sign convention used here:
    --   current < 0: charging, current > 0: discharging.
    if current_ma < -5 then
        return "charging"
    end
    if current_ma > 5 then
        return "discharging"
    end
    return "idle"
end

local function cleanup()
    if gauge then
        pcall(function()
            gauge:close()
        end)
        gauge = nil
    end
end

local function run()
    local port = int_arg("port", 0)
    local sda = int_arg("sda", 14)
    local scl = int_arg("scl", 13)
    bus = a.bus or i2c.new(port, sda, scl, FREQ_HZ)

    local opts = {
        bus = bus,
        chip = CHIP,
        freq_hz = FREQ_HZ,
    }
    if I2C_ADDR then
        opts.addr = I2C_ADDR
    end

    gauge = fuel_gauge.new(opts)
    print(string.format(
        "[fuel_gauge] opened chip=%s addr=0x%02X",
        gauge:chip(), gauge:address()
    ))

    for i = 1, SAMPLE_COUNT do
        local sample = gauge:read()
        local state = current_state_text(sample.current_ma)
        local current_str = sample.current_ma
            and string.format(" current=%dmA", sample.current_ma)
            or ""
        local state_str = state and string.format(" state=%s", state) or ""
        print(string.format(
            "[fuel_gauge] #%d soc=%d%% voltage=%dmV%s%s",
            i,
            sample.soc,
            sample.voltage_mv,
            current_str,
            state_str
        ))
        delay.delay_ms(INTERVAL_MS)
    end
end

local ok, err = xpcall(run, debug.traceback)
cleanup()
if not ok then
    error(err)
end
