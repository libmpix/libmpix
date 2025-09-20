#!/bin/sh

cd "$(dirname "$0")/.."

echo "/* SPDX-License-Identifier: Apache-2.0 */"
echo "/* Genreated with scripts/genlist.sh */"

c2op() {
    awk -F '[( ),]' '

        BEGIN {
            print ""
            print "#define MPIX_FOR_EACH_OP(fn) \\"
        }

        $1 == "MPIX_REGISTER_OP" {
            print "\t""fn("toupper($2)", "tolower($2)") \\"
        }

        END {
            print "\t/* end */"
        }

    ' "$@"
}

c2str() {
    awk -v enum="$1" -v label="$2" '
    	BEGIN {
        	prefix = "MPIX_" toupper(label) "_"
        	print ""
        	print "#define MPIX_FOR_EACH_" toupper(label) "(fn) \\"
        }

        (!enum && sub("#define " prefix, "")) || (enum && sub("\t" prefix, "")) {
            gsub(",", "")
            print "\t""fn("toupper($1)", "tolower($1)") \\"
        }

        END {
            print "\t/* end */"
        }

    ' include/mpix/*.h
}

c2op "$@"

c2str 0 fmt
c2str 1 kernel
c2str 1 jpeg
c2str 1 cid
