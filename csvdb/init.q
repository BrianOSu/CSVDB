// CSV-backed table adapter. The directory contains one CSV file per column;
// each file is named <column>.csv and has a header row with that column name for added verification

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

// Query callback: find referenced columns, read their files, and execute the
// functional qSQL query against the assembled in-memory table.
csvQuery:{[columns; typeMap; `T!([t]); c; b; a; v]
    refs:distinct raze tab.fv each (c;b;a);
    needed:$[(c~())|(c~()!())|0b~c;
                columns;
                columns where columns in refs];
    needed:$[count needed;
                needed;
                first columns];
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

// Build a T adapter over <column>.csv files in dir. columns and types are
// parallel symbol/type-code lists; only referenced files are read.
csvT:{[dir;columns;types]
    if[(0=count columns)|((count columns)<>count types);
        '"columns and types must have the same non-zero length"];
    if[(count distinct columns)<>count columns;
        '"column names must be unique"];
    typeMap:columns!upper types;
    tab.mkT ([f:csvQuery[columns;typeMap; ];
              m:csvMeta[columns;typeMap; ];
              t:dir])
 };

// Build a partitioned CSV table adapter. Each directory in dirs contains one
// partition's per-column CSV files; columns and types describe their schema.
// partitions supplies the corresponding partition values (for example, dates),
// so qSQL can filter partitions before querying their CSV table adapters.
csvP:{[dirs; columns; types; partitions]
    tab.mkP partitions!csvT[ ;columns; types] each dirs
 };

export:([csvT; csvP])
