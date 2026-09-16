# Archive a readable report before replacing it.  For name.ext, find the
# highest existing name.N.ext and move name.ext to name.(N+1).ext.
function next_report_number --argument-names report
    set -l directory (dirname -- "$report")
    set -l name (basename -- "$report")
    set -l parts (string split -r -m1 . -- "$name")
    if test (count $parts) -ne 2
        echo "Readable report needs an extension: $report" >&2
        return 2
    end
    set -l stem $parts[1]
    set -l extension $parts[2]
    set -l highest 0

    for candidate in (find "$directory" -maxdepth 1 -type f -name "$stem.*.$extension" -printf '%f\n')
        set -l prefix "$stem."
        set -l suffix ".$extension"
        set -l number (string sub -s (math (string length -- "$prefix") + 1) \
            -l (math (string length -- "$candidate") - (string length -- "$prefix") - (string length -- "$suffix")) \
            -- "$candidate")
        if string match -qr '^[1-9][0-9]*$' -- "$number"
            if test "$number" -gt "$highest"
                set highest "$number"
            end
        end
    end

    math "$highest + 1"
end

function rotate_report --argument-names report number
    if not test -f "$report"
        return 0
    end
    set -l directory (dirname -- "$report")
    set -l name (basename -- "$report")
    set -l parts (string split -r -m1 . -- "$name")
    if test -z "$number"
        set number (next_report_number "$report")
    end
    if not string match -qr '^[1-9][0-9]*$' -- "$number"
        echo "Invalid report archive number: $number" >&2
        return 2
    end
    set -l archived "$directory/$parts[1].$number.$parts[2]"
    if test -e "$archived"
        echo "Report archive already exists: $archived" >&2
        return 2
    end
    mv -- "$report" "$archived"
    echo "Archived $report as $archived"
end
