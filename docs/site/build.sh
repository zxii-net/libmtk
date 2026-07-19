#!/bin/sh
# Builds the website published at libmtk.dev.zxii.net into _site/:
# the markdown pages (README, tutorial, pitfalls) rendered with
# pandoc, and the Doxygen API reference under /docs/.  Run from the
# repository root; needs pandoc and doxygen.
set -eu

test -f Doxyfile || { echo 'run from the repository root' >&2; exit 1; }

rm -rf _site
mkdir -p _site/tutorial _site/docs

# render <src.md> <dest.html> <css-relative-path> <repo-dir-of-src>
render() {
  title=$(awk '/^# /{ sub(/^# +/, ""); print; exit }' "$1")
  MDLINKS_BASE=$4 pandoc -f gfm -t html5 --standalone \
    --lua-filter docs/site/mdlinks.lua \
    --metadata pagetitle="${title:-libmtk}" \
    -c "$3" -o "$2" "$1"
}

render README.md          _site/index.html         site.css    ''
render docs/pitfalls.md   _site/docs/pitfalls.html ../site.css docs
for md in tutorial/*.md; do
  render "$md" "_site/tutorial/$(basename "${md%.md}").html" ../site.css tutorial
done
cp _site/tutorial/README.html _site/tutorial/index.html
cp _site/index.html _site/README.html

cp docs/site/site.css _site/
cp -r tutorial/img _site/tutorial/

doxygen
cp -r docs/api/html/. _site/docs/
