# CSVDB

CSVDB is a KDB-X module that exposes CSV data as qSQL-readable table
adapters. It supports both directories of per-column CSV files and individual
flat, multi-column CSV files, including date-partitioned datasets. Instead of
first importing the files into native kdb+ tables, callers can create an
adapter and query it with qSQL.

CSVDB is useful for exploring or querying CSV-based historical datasets,
working with column-oriented exports, and integrating CSV partitions into qSQL
workflows. It assembles the data in memory to run queries; it does not turn CSV
files into a persistent native HDB. For per-column layouts it reads the
referenced columns, while a flat CSV file is parsed as a whole.

## Requirements

- KDB-X with its module framework (`use`)
- The KX Parquet module, `kx.pq`, available on the module search path

CSVDB uses the Parquet module's table-adapter helpers. Make sure the search
path includes both the directory containing the `csvdb` module and the
directory containing `kx.pq` (for example, `$HOME/.kx/mod` in a standard
KDB-X setup).

## CSV layout

Each table directory contains one CSV file per column. A file is named
`<column>.csv`, has a single-column header matching the column name, and has
one value per row:

```text
data/2026.11.05/t/
├── sym.csv
├── ex.csv
├── price.csv
├── size.csv
└── time.csv
```

For example, `sym.csv` begins:

```csv
sym
akn
abe
ale
```

All files in a directory must have the same number of data rows. The supplied
column names and type codes define the schema and order. Type codes use q's
`0:` CSV conventions, for example `s` for symbol, `f` for float, `j` for long,
and `t` for time. CSVDB uses the header to verify each file's column name.

Alternatively, a flat CSV file can contain all columns in each row, with a
header row listing the column names. For example, `data/2026.11.07/t.csv` begins:

```csv
sym,ex,price,size,time
akn,j,250.9591,7227,02:32:17.036
abe,l,371.8678,2154,01:44:56.936
ale,k,311.5099,4387,00:25:23.210
```

Flat files use comma delimiters and must have headers that match the supplied
column names and order.

## Load the module

With the project and KX module directories on the module search path:

```q
csvdb:use`csvdb;
```

For example, start q from the project root with:

```sh
QPATH="$PWD:$HOME/.kx/mod" q
```

Adjust the second path to the location where the KX Parquet module is
installed. The module exports `csvT` for one CSV source (a per-column
directory or a flat file) and `csvP` for a partitioned dataset.

## Single-source table

Create an adapter by passing a directory or flat CSV file path, column names,
and matching type codes:

```q
columns:`sym`ex`price`size`time;
types:"ssfjt";
t:csvdb.csvT[`:data/2026.11.05/t/;columns;types];

select from t
select sym,price from t where size>9000
select avg price by ex from t
meta t
```

The type string is parallel to the column list: here `sym` and `ex` are
symbols, `price` is a float, `size` is a long, and `time` is a time.

The same API accepts a flat file with those columns:

```q
t:csvdb.csvT[`:data/2026.11.07/t.csv;columns;types];
select sym,price from t where size>9000
```

## Partitioned table

Use `csvP` when each partition is stored in its own directory or flat CSV
file. `dirs` contains the path for each partition, and `partitions` is a table
with one row per path. Its columns (commonly a date column) identify each
partition and are available to qSQL:

```q
dirs:`:data/2026.11.05/t/`:data/2026.11.06/t/`:data/2026.11.07/t.csv;
partitions:([]date:2026.11.05 2026.11.06 2026.11.07);
t:csvdb.csvP[dirs;columns;types;partitions];

select from t where date=2026.11.05
select sym,price from t where date within (2026.11.05;2026.11.07)
meta t
```

Partition filtering allows qSQL to select relevant partitions before querying
their CSV data. Each partition must use the same column names and types;
directory partitions must also have matching row counts across their column
files.

## Build the C++ Plugin

Build the plugin shared library using `cmake`:

```sh
cmake -B build && cmake --build build
```

This generates `install/csvdb.so`.

## Run the tests

The integration tests use the sample partitions under `data/` and compare
CSVDB results with reference tables read directly from the CSV files:

```sh
QPATH="$PWD:$HOME/.kx/mod" q test/csvdb.q -q
QPATH="$PWD:$HOME/.kx/mod" q test/test_plugin.q -q
```

Set `QPATH` to include both this project and the installed KX module
directories. The tests cover single-directory queries, metadata, projections,
filters, grouped aggregation, and partitioned queries.
