#!/bin/bash

# Repeatedly apply the extended regex $2 (with capture groups) to the string $1, replacing each
# match with template $3 in which \1, \2, ... refer to the capture groups, and echo the result.
# The scan resumes after each replacement, so already-substituted text (ANSI codes) is not
# rematched. The regex is held in a variable and used unquoted, as bash regex matching requires.
substitute() {
    local rest=$1 re=$2 repl=$3
    local out= match result i
    while [[ $rest =~ $re ]]; do
        match=${BASH_REMATCH[0]}
        result=$repl
        # Expand \1..\9 in the template from the capture groups.
        for (( i = 1; i < ${#BASH_REMATCH[@]}; i++ )); do
            result=${result//\\$i/${BASH_REMATCH[i]}}
        done
        # Append everything before the match, then the rewritten match.
        out=$out${rest%%"$match"*}$result
        rest=${rest#*"$match"}
    done
    printf '%s' "$out$rest"
}

# Print the given inline MarkDown text, replacing _italics_/*italics* with italicized text and
# **bold** with bold text using ANSI escape codes. For `code` text use an ANSI light gray background.
inline_text() {
    local text=$*
    # Fast path: nothing to do if the line has no markup delimiters at all.
    if [[ $text != *['*_`']* ]]; then
        printf '%s\n' "$text"
        return
    fi
    local e=$'\033'
    # **bold** -> bold (ANSI 1), reset with 22. Process before *italics* / _italics_ so the
    # inner characters of the ** delimiters are not mismatched as single-* emphasis.
    text=$(substitute "$text" '\*\*([^*]+)\*\*'           "${e}[1m\1${e}[22m")
    text=$(substitute "$text" '\*([^[:space:]*][^*]*)\*'  "${e}[3m\1${e}[23m")
    text=$(substitute "$text" '(^|[^[:alnum:]_])_([^_]+)_($|[^[:alnum:]_])' "\1${e}[3m\2${e}[23m\3")
    text=$(substitute "$text" '`([^`]+)`'                 "${e}[48;5;253;38;5;236m \1 ${e}[39;49m")
    printf '%s\n' "$text"
}

# Print a header line - arguments are of the form ## Header Text
# The length of the first argument determines the header level
header() {
    local lines="━─┈┄" # Different underlines for h1, h2, h3 and h4
    local level=${#1} # The number of '#' characters determines the level
    shift
    local underline=${lines:level-1:1} # Select the appropriate underline character
    inline_text "**${*}**" # Print the header text in bold
    printf '%*s\n' "${COLUMNS}" '' | tr ' ' "$underline" # Print the underline
}


thematic_break() {
    printf '%*s\n' "${COLUMNS}" '' | tr ' ' '─' # Print a horizontal line across the terminal
}

# Print the display width of a cell: its character count after removing any ANSI escape
# sequences (ESC[ ... <letter>), so colored/bold/italic text measures by what is shown.
cell_width() {
    local s=$1 stripped
    stripped=$(substitute "$s" $'\033''\[[0-9;]*[A-Za-z]' '')
    printf '%s' "${#stripped}"
}

# Render a GitHub-flavoured MarkDown table. Each argument is one raw table line (with leading
# and trailing '|'). The second line is the |---| delimiter row and is used only to detect the
# table, not printed. Cells are formatted with inline_text; columns are sized to their widest cell.
render_table() {
    local -a lines=("$@")
    local lightgray=$'\033''[38;5;250m' reset=$'\033''[0m'
    local i j row cell

    # A real GFM table has a delimiter row (e.g. |---|:--:|) as its second line. If it is
    # missing, this is not a table: print the header line as ordinary text and re-process the
    # remaining lines normally (one of them may yet begin a properly-delimited table).
    if [[ ${#lines[@]} -lt 2 || ! ${lines[1]} =~ ^\|[[:space:]:|-]*-[[:space:]:|-]*\|$ ]]; then
        inline_text "${lines[0]}"
        if (( ${#lines[@]} > 1 )); then
            printf '%s\n' "${lines[@]:1}" | mdcat_stream
        fi
        return
    fi

    # Parse every line (except the delimiter row, index 1) into the cells[] flat array,
    # tracking the number of rows and the maximum number of columns seen.
    local -a cells=() widths=()
    local ncols=0 nrows=0
    for (( i = 0; i < ${#lines[@]}; i++ )); do
        (( i == 1 )) && continue            # skip the |---|---| delimiter row
        row=${lines[i]}
        row=${row#|}; row=${row%|}          # drop the leading and trailing pipes
        local -a fields=()
        local IFS='|'
        read -r -a fields <<< "$row"
        unset IFS
        local col=0
        for cell in "${fields[@]}"; do
            # Trim surrounding whitespace, then format inline markup.
            cell=${cell#"${cell%%[![:space:]]*}"}
            cell=${cell%"${cell##*[![:space:]]}"}
            cell=$(inline_text "$cell")     # adds a trailing newline; strip it below
            cell=${cell%$'\n'}
            cells[nrows * 64 + col]=$cell    # 64 = generous max columns per row
            (( col++ ))
        done
        (( col > ncols )) && ncols=$col
        (( nrows++ ))
    done

    # Column widths: the maximum cell width in each column.
    for (( j = 0; j < ncols; j++ )); do widths[j]=0; done
    for (( i = 0; i < nrows; i++ )); do
        for (( j = 0; j < ncols; j++ )); do
            local w; w=$(cell_width "${cells[i * 64 + j]}")
            (( w > widths[j] )) && widths[j]=$w
        done
    done

    # Total table width: sum of columns plus two-space separators between them.
    local total=0
    for (( j = 0; j < ncols; j++ )); do (( total += widths[j] )); done
    (( total += (ncols - 1) * 2 ))

    # Build a full-width run of '─' line-drawing characters for the separators.
    local hline
    hline=$(printf '%*s' "$total" '' | tr ' ' '─')

    # Print each row, padding cells to their column width and joining with two spaces.
    # Row 0 is the header: render its cells bold, then underline the whole table.
    for (( i = 0; i < nrows; i++ )); do
        local line=
        for (( j = 0; j < ncols; j++ )); do
            cell=${cells[i * 64 + j]}
            local pad=$(( widths[j] - $(cell_width "$cell") ))
            (( i == 0 )) && cell=$'\033'"[1m${cell}"$'\033'"[22m"   # bold header cells
            (( j > 0 )) && line+='  '
            line+=$cell
            (( pad > 0 )) && line+=$(printf '%*s' "$pad" '')
        done
        printf '%s\n' "$line"
        if (( i == 0 )); then
            printf '%s\n' "$hline"                         # header underline (default colour)
        elif (( i < nrows - 1 )); then
            printf '%s%s%s\n' "$lightgray" "$hline" "$reset"  # light-gray row separator
        fi
    done
    printf '\n'   # blank line after the table
}

# Render MarkDown read from standard input.
mdcat_stream() {
    local line
    while IFS= read -r line; do
        # Capture ## part and the rest of the line separately
        if [[ $line =~ ^(#+)\ (.+) ]]; then
            header "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}"
        elif [[ $line =~ ^---$ ]]; then
            thematic_break
        elif [[ $line == '|'*'|' ]]; then
            # A table: read all following pipe-rows up to a blank line (or EOF) in one go.
            local -a table=("$line")
            while IFS= read -r line && [[ -n $line ]]; do
                table+=("$line")
            done
            render_table "${table[@]}"
        else
            inline_text "$line"
        fi
    done
}

# Render MarkDown from the given files, or from standard input if no files are given.
mdcat () {
    if (( $# > 0 )); then
        cat -- "$@" | mdcat_stream
    else
        mdcat_stream
    fi
}



