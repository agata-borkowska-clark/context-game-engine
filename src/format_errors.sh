#!/bin/bash

sed -E 's/([^(]+)\(([0-9]+),([0-9]+)\): error/\1:\2:\3: error/'
