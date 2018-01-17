#!/bin/bash

# Sets ignore properties in the repository from information in
# .cvsignore files.

for ifile in `find . -name .cvsignore`; do
#  echo $ifile;
#  echo $ifile | sed -e 's/\/\.cvsignore//g';
  svn propset svn:ignore -F $ifile `echo $ifile | sed -e 's/\/\.cvsignore//g'`
done

echo
echo "Please do 'svn -m \"Added ignore properties\" commit'"
