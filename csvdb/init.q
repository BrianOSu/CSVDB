// CSV-backed table adapter for a flat CSV file or a directory of per-column
// CSV files. Each per-column file is named <column>.csv and has a header row.

tab:.q.use`kx.pq.t

// Library function to read indices of a given type from a CSV file that contains only 1 column
// @param FilePath Hsym  - Path to csv file
// @param ColType  Char  - Type to read the data in as
// @param Indices  Longs - Rows to read from csv file
// @return Table         - CSV file read in as a 1 column table
csvColReader:(`$"install/csvdb") 2:(`read_csv; 3)

// Read a single column file, validate its header, and cast values to the
// declared q type. The trailing space type skips any extra parsed field.
csvColumn:{[dir; typeCode; column]
    csvColumnRows[dir; typeCode; column; (::)]
 };

// Read one column, validate its row count, then retain only matching rows.
csvColumnRows:{[dir;typeCode;column;indices]
    path:.Q.dd[dir; .Q.dd[column; `csv]];
    flip (enlist column)!enlist csvColReader[path; typeCode; indices]
 };

// Read a flat, multi-column CSV file and verify its header against the schema.
csvFile:{[path;columns;types]
    data:(types;enlist ",") 0: path;
    if[not (cols data)~columns;
        '"CSV file header does not match the declared columns"];
    data
 };

// Execute a query against a flat CSV file.
queryFile:{[columns;typeMap;dir;c;b;a;v]
    (.) (?; csvFile[dir;columns;typeMap columns]; c; b; a),v
 };

// Filter predicate columns first, then load only referenced rows from the
// remaining columns before evaluating the select and by expressions.
queryColFiltered:{[columns; typeMap; dir; c; b; a; v; (refs;whereCols)]
    if[not count needed:columns where columns in refs;
       '"Invalid columns requested"]
    if[1<>count distinct count each whereData:csvColumn[dir;;]'[typeMap whereCols; whereCols];
       '"column CSV files have different row counts"];
    whereData:?[(^/)whereData; c; (); `i]; // Indices to apply to cols
    if[1<>count distinct count each data:csvColumnRows[dir; ; ; whereData]'[typeMap needed; needed];
       '"column CSV files have different row counts"];
    (.) (?; (^/)data; (); b; a),v
 };

// Load all referenced columns and let qSQL apply the where clause.
queryCol:{[columns;typeMap;dir;c;b;a;v;refs]
    if[not count needed:columns where columns in refs;
        '"Invalid columns requested"];
    if[1<>count distinct count each data:csvColumn[dir;;]'[typeMap needed; needed];
        '"column CSV files have different row counts"];
    (.) (?; (^/)data; c; b; a),v
 };


// Query callback: find referenced columns, read their files, and execute the
// functional qSQL query against the assembled in-memory table.
csvQuery:{[columns; typeMap; isFile; `T!([t]); c; b; a; v]
    if[isFile;
        :queryFile[columns;typeMap;t;c;b;a;v]];
    // Identify required columns from c=where, b=by, and a=select expressions.
    whereCols:distinct tab.fv c;
    refs:distinct raze (whereCols; tab.fv b; tab.fv a);
    // Can filter columns being read if where clause supplied
    if[count whereCols:columns where columns in whereCols;
        :queryColFiltered[columns; typeMap; t; c; b; a; v; (refs;whereCols)]];
    queryCol[columns; typeMap; t; c; b; a; v; refs]
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
