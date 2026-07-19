-- Pandoc Lua filter used by docs/site/build.sh: rewrites the
-- repo-relative links for the static site.  Markdown pages become
-- .html; anything that only exists in the repository (sources,
-- LICENSE, directories) points at GitHub instead.  The document's
-- directory inside the repo comes in via $MDLINKS_BASE.

local repo = 'https://github.com/zxii-net/libmtk'
local base = os.getenv('MDLINKS_BASE') or ''

local function is_external(t)
  return t:match('^%a[%w+.-]*:') or t:match('^#') or t:match('^//')
end

function Link(el)
  local t = el.target
  if is_external(t) then
    return el
  end

  local path, anchor = t, ''
  local h = t:find('#', 1, true)
  if h then
    path, anchor = t:sub(1, h - 1), t:sub(h)
  end

  if path:match('%.md$') then
    el.target = path:gsub('%.md$', '.html') .. anchor
    return el
  end

  -- directories rendered as part of the site keep their local link
  if path == '' or path == 'tutorial' or path == 'tutorial/' then
    return el
  end

  -- everything else lives only in the repository
  local full = path
  if base ~= '' then
    full = base .. '/' .. path
  end
  full = full:gsub('^%./', '')
  repeat
    local n
    full, n = full:gsub('[^/]+/%.%./', '', 1)
  until n == 0
  local kind = path:match('/$') and 'tree' or 'blob'
  el.target = repo .. '/' .. kind .. '/master/' .. full
  return el
end
