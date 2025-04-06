set -eu

PROGNAME="$(basename $0)"
BASEDIR="$(cd $(dirname $0); pwd)"
PROJDIR="$(cd $BASEDIR/..; pwd)"

# cc | hh
echo 'Formatting *.[cc|hh]...'
find $PROJDIR -name '*.cc' -o -name '*.hh' | grep -v './vendor/' | xargs clang-format -i

# TODO: add other tasks here
