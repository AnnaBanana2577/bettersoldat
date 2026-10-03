# A release as a Discord webhook's message: one embed, in the menu's ember, titled with
# the version and linked to the release, its notes as the description, and the game's
# downloads for each platform; the game's icon is the sender's avatar. Run over `gh release view
# --json name,tagName,url,body,publishedAt,isPrerelease,assets` (discord-release.yml).
#
# The notes are the tag's message (docs/git.md, Releases), written as plain text for
# git: its lines are wrapped with an indent under each "- ", which Discord would show as
# broken lines, so they are joined; its headings (Added, Changed, Fixed) are made bold
# and its first line ("Changes since v0.6.1") small. An embed's description holds 4096
# characters: a longer one is cut, and the rest is a link away.

def notes:
  gsub("\r"; "")
  | gsub("\n +(?<rest>[^ \n-])"; " \(.rest)")
  | gsub("(?m)^(?<h>Added|Changed|Fixed|Removed|Notes)$"; "**\(.h)**")
  | gsub("(?m)^(?<l>Changes since [^\n]+)$"; "-# \(.l)")
  | gsub("^\\s+|\\s+$"; "");

def cut($url):
  if length > 3900 then .[0:3850] + "…\n\n[Read the rest on GitHub](\($url))" else . end;

# the game itself for each platform: not the update package, not the server's
def downloads:
  [.assets | sort_by(.name | test("windows") | not) | .[]
   | select((.name | test("\\.(zip|tar\\.gz)$")) and (.name | test("-(patch|server)\\.") | not))
   | "[\(if (.name | test("windows")) then "Windows" else "Linux" end)](\(.url))"]
  | join("  ·  ");

. as $r
# the files as the release's tag has them, not main's: the URL is new with each release,
# so neither GitHub's raw cache nor Discord's can hand back an older icon
| ($ENV.REPO_RAW // "https://raw.githubusercontent.com/soldatreloaded/soldatreloaded/\($r.tagName)") as $raw
| {
    username: "SoldatReloaded",
    avatar_url: "\($raw)/runtime/data/icon.png",
    embeds: [{
      title: "SoldatReloaded \($r.tagName)\(if $r.isPrerelease then " (pre-release)" else "" end) is out",
      url: $r.url,
      color: 15224862,
      description: ($r.body | notes | cut($r.url)),
      fields: [{
        name: "Download",
        value: ((($r | downloads) | if . == "" then "" else . + "\n" end)
                + "Already playing? The launcher updates itself to this version.")
      }],
      footer: {text: "soldatreloaded/soldatreloaded"},
      timestamp: $r.publishedAt
    }]
  }
