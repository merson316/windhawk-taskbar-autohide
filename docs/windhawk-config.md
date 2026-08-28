# Windhawk injection configuration

Windhawk's engine injects into all processes by default, minus built-in
exclusions. A mod's `@include` controls where the *mod* runs, not where the
*engine* is injected, so per-mod targeting does not protect games.

## Applied configuration

Settings -> Advanced settings -> More advanced settings -> Process exclusion
list, additive to Windhawk's built-in exclusions:

    <drive>:\SteamLibrary\*
    <drive>:\SteamLibrary\*
    <drive>:\SteamLibrary\*
    <drive>:\<launcher>\*
    <drive>:\<launcher>\*
    C:\XboxGames\*

## Verify

The setting is stored under the **Engine** key, not `Windhawk\Settings`, as a
single pipe-separated `REG_SZ`:

    /mnt/c/Windows/System32/reg.exe query \
      "HKLM\SOFTWARE\Windhawk\Engine\Settings" /v Exclude | tr -d '\r'

Expected:

    Exclude    REG_SZ    <drive>:\SteamLibrary\*|<drive>:\SteamLibrary\*|<drive>:\SteamLibrary\*|<drive>:\<launcher>\*|<drive>:\<launcher>\*|C:\XboxGames\*

An empty `Include` value alongside it is correct: it means default broad
injection minus the exclusions above, rather than an allowlist.

## Maintenance

Adding a new game library root means adding one more line in the UI. Games
installed inside the roots above are covered automatically.

## Rejected alternative

Excluding `*` and allowlisting only `explorer.exe`, `<other-app>.exe` and
`StartMenuExperienceHost.exe` is more contained, but every future mod install
would silently do nothing until its targets were added by hand, and blanket
exclusion delays injection in a way that may break some mods.
