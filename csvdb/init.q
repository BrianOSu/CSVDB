// CSV-backed table adapter for a flat CSV file or a directory of per-column
// CSV files. Each per-column file is named <column>.csv and has a header row.

tab:.q.use`kx.pq.t

// Read a single column file, validate its header, and cast values to the
// declared q type. The trailing space type skips any extra parsed field.
csvColumn:{[dir; typeCode; column]
    path:.Q.dd[dir; .Q.dd[column; `csv]];
    data:(enlist typeCode; enlist ",") 0: path;
    if[not (cols data)~enlist column;
        '"column CSV header does not match its name"];
    data
 };

// Read a flat, multi-column CSV file and verify its header against the schema.
csvFile:{[path;columns;types]
    data:(types;enlist ",") 0: path;
    if[not (cols data)~columns;
        '"CSV file header does not match the declared columns"];
    data
 };

// Query callback: find referenced columns, read their files, and execute the
// functional qSQL query against the assembled in-memory table.
csvQuery:{[columns; typeMap; isFile; `T!([t]); c; b; a; v]
    if[isFile;
        source:csvFile[t;columns;typeMap columns];
        :(.) (?;source;c;b;a),v];
    // Functional query arguments are c=where, b=by, and a=select expressions.
    refs:distinct raze tab.fv each (c;b;a);
    needed:columns where columns in refs;
    data:{[dir;typeMap;column]
            csvColumn[dir;typeMap column;column]
          }[t;typeMap] each needed;
    rowCounts:count each data;
    if[not all rowCounts=first rowCounts;
        '"column CSV files have different row counts"];
    (.) (?; (^/)data; c; b; a),v
 };

// Return either the available names or the requested columns for the T
// metadata callback. This is used by q's meta to discover column types.
csvMeta:{[columns;typeMap;`T!([t]);requested]
    if[0=count requested;
        :columns];
    source:flip requested!{[typeMap;column]
                                0#(typeMap column)$()
                            }[typeMap] each requested;
    tab.mt[source;requested]
 };

// Build a T adapter over a flat CSV file or <column>.csv files in a directory.
// columns and types are parallel symbol/type-code lists.
csvT:{[dir;columns;types]
    if[(0=count columns)|((count columns)<>count types);
        '"columns and types must have the same non-zero length"];
    if[(count distinct columns)<>count columns;
        '"column names must be unique"];
    sourceType:type key dir;
    if[not sourceType in -11 11h;
        '"source path must be an existing CSV file or directory"];
    isFile:-11h=sourceType;
    typeMap:columns!types;
    tab.mkT ([f:csvQuery[columns;typeMap;isFile; ];
              m:csvMeta[columns;typeMap; ];
              t:dir])
 };

// Build a partitioned CSV table adapter. Each path in dirs identifies either
// a flat CSV file or a directory of per-column files for one partition.
// columns and types describe the shared schema.
// partitions supplies the corresponding partition values (for example, dates),
// so qSQL can filter partitions before querying their CSV table adapters.
csvP:{[dirs; columns; types; partitions]
    tab.mkP partitions!csvT[ ;columns; types] each dirs
 };

export:([csvT; csvP])
