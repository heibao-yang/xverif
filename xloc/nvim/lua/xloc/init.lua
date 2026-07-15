local M = {}

local config = {
  auto_enable = true,
  repo_root = nil,
}

local record_cache = {}
local loc_id_pattern = "L_" .. string.rep("[A-Za-z0-9]", 6) .. "[A-Za-z0-9]*"

local function notify(message)
  vim.notify("xloc: " .. message, vim.log.levels.WARN)
end

local function location_id_under_cursor()
  local word = vim.fn.expand("<cWORD>")
  local id = word:match(loc_id_pattern)
  if id then
    return id
  end
  return vim.api.nvim_get_current_line():match(loc_id_pattern)
end

local function map_file()
  local log_file = vim.api.nvim_buf_get_name(0)
  if log_file == "" then
    return nil
  end
  local path = log_file .. ".xloc.jsonl"
  return vim.fn.filereadable(path) == 1 and path or nil
end

local function cache_entry(path)
  path = vim.fn.fnamemodify(path, ":p")
  local stat = vim.uv.fs_stat(path)
  if not stat then
    return nil
  end
  local mtime = stat.mtime.sec * 1000000000 + stat.mtime.nsec
  local entry = record_cache[path]
  if not entry or entry.mtime ~= mtime then
    entry = { mtime = mtime, records = {} }
    record_cache[path] = entry
  end
  return entry
end

local function lookup_record(path, id)
  local entry = cache_entry(path)
  if not entry then
    return nil
  end
  if entry.records[id] then
    return entry.records[id]
  end

  for line in io.lines(path) do
    local ok, record = pcall(vim.json.decode, line)
    if ok and type(record) == "table" and record.loc_id == id then
      entry.records[id] = record
      return record
    end
  end
  return nil
end

local function record_file(record)
  local file = record.file or record.path or record.filename
  return type(file) == "string" and file or nil
end

local function record_line(record)
  local line = tonumber(record.line) or 1
  return math.max(line, 1)
end

local function absolute_path(path)
  return path:match("^/") or path:match("^[A-Za-z]:[\\/]")
end

local function resolve_path(file, sidecar)
  if not file or file == "" then
    return nil
  end
  if absolute_path(file) then
    return vim.fn.fnamemodify(file, ":p")
  end

  local roots = {}
  if config.repo_root and config.repo_root ~= "" then
    table.insert(roots, config.repo_root)
  end
  table.insert(roots, vim.fn.fnamemodify(sidecar, ":p:h"))
  table.insert(roots, vim.fn.getcwd())

  for _, root in ipairs(roots) do
    local path = vim.fn.fnamemodify(root .. "/" .. file, ":p")
    if vim.fn.filereadable(path) == 1 then
      return path
    end
  end

  if config.repo_root and config.repo_root ~= "" then
    return vim.fn.fnamemodify(config.repo_root .. "/" .. file, ":p")
  end
  return vim.fn.fnamemodify(vim.fn.fnamemodify(sidecar, ":p:h") .. "/" .. file, ":p")
end

local function native_gf()
  local ok, error_message = pcall(vim.cmd, "normal! gf")
  if not ok then
    notify("native gf failed: " .. error_message)
  end
end

function M.gf()
  local id = location_id_under_cursor()
  if not id then
    native_gf()
    return
  end

  local sidecar = map_file()
  if not sidecar then
    native_gf()
    return
  end

  local record = lookup_record(sidecar, id)
  if not record then
    notify("loc_id not found: " .. id .. " in " .. sidecar)
    return
  end

  local file = record_file(record)
  if not file then
    notify("record has no file/path/filename field: " .. id)
    return
  end

  local path = resolve_path(file, sidecar)
  if vim.fn.filereadable(path) ~= 1 then
    notify("source file not readable: " .. path)
    return
  end

  vim.cmd.edit(vim.fn.fnameescape(path))
  vim.api.nvim_win_set_cursor(0, { record_line(record), 0 })
  vim.cmd("normal! zz")
end

function M.maybe_map_buffer(buffer)
  if not config.auto_enable then
    return
  end
  buffer = buffer or vim.api.nvim_get_current_buf()
  local name = vim.api.nvim_buf_get_name(buffer)
  if vim.fn.fnamemodify(name, ":e") ~= "log" or vim.fn.filereadable(name .. ".xloc.jsonl") ~= 1 then
    return
  end
  vim.keymap.set("n", "gf", M.gf, { buffer = buffer, silent = true, desc = "xloc: jump to source location" })
end

function M.setup(options)
  options = options or {}
  config.auto_enable = options.auto_enable
  if config.auto_enable == nil then
    config.auto_enable = vim.g.xloc_auto_enable ~= 0
  end
  config.repo_root = options.repo_root or vim.g.xloc_repo_root

  vim.api.nvim_create_user_command("XlocGF", M.gf, { force = true })
  local group = vim.api.nvim_create_augroup("xloc_gf", { clear = true })
  vim.api.nvim_create_autocmd({ "BufReadPost", "BufNewFile" }, {
    group = group,
    pattern = "*.log",
    callback = function(args)
      M.maybe_map_buffer(args.buf)
    end,
  })
  M.maybe_map_buffer()
end

return M
