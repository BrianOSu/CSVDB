// Run from the project root with:
// QPATH="$PWD:$HOME/.kx/mod" /home/brian/.kx/bin/q test/csvdb.q -q

csvdb:use`csvdb;

columnNames:`sym`ex`price`size`time;
typeCodes:"ssfjt";
typeMap:columnNames!upper typeCodes;
dates:2026.11.05 2026.11.06 2026.11.07;
dirs:`:data/2026.11.05/t/`:data/2026.11.06/t/;
flatPath:`:data/2026.11.07/t.csv;

check:{[condition;label]
    if[not condition;
        -1 "FAIL: ",label;
        '"CSVDB test failed"];
    -1 "PASS: ",label
 };

readColumn:{[dir;column;typeCode]
    values:1_read0 .Q.dd[dir;.Q.dd[column;`csv]];
    typeCode$values
 };

readTable:{[dir;names;typesByName]
    flip names!{[dir;typesByName;column]
                   readColumn[dir;column;typesByName column]
                 }[dir;typesByName] each names
 };

withPartition:{[partition;table]
    names:`date,cols table;
    columns:(enlist (count table)#partition),value flip table;
    flip names!columns
 };

expected05:readTable[first dirs;columnNames;typeMap];
expected06:readTable[last dirs;columnNames;typeMap];
expected07:(upper typeCodes;enlist ",") 0: flatPath;
single:csvdb.csvT[first dirs;columnNames;typeCodes];
actual05:select from single;

check[(count actual05)=10000;"single partition row count"];
check[actual05~expected05;"single partition values and column order"];
check[(meta single)~(meta expected05);"single partition metadata"];
check[(select sym,price from single)~(select sym,price from expected05);
      "column projection"];
check[(select from single where size>9000)~(select from expected05 where size>9000);
      "row filtering"];
check[(select avg price by ex from single)~(select avg price by ex from expected05);
      "grouped aggregation"];

flat:csvdb.csvT[flatPath;columnNames;typeCodes];
actualFlat:select from flat;
check[(count actualFlat)=10000;"flat CSV row count"];
check[actualFlat~expected07;"flat CSV values and column order"];
check[(meta flat)~(meta expected07);"flat CSV metadata"];
check[(select sym,price from flat where size>9000)~
      (select sym,price from expected07 where size>9000);
      "flat CSV projection and filtering"];

partitionKeys:([]date:dates);
partitionSources:dirs,enlist flatPath;
partitioned:csvdb.csvP[partitionSources;columnNames;typeCodes;partitionKeys];
expectedPartitions:withPartition[2026.11.05;expected05],
                   withPartition[2026.11.06;expected06],
                   withPartition[2026.11.07;expected07];
actualPartitions:select from partitioned;

check[(meta partitioned)~(meta expectedPartitions);
      "partitioned metadata"];
check[(count actualPartitions)=30000;"combined partition row count"];
check[actualPartitions~expectedPartitions;"combined partition values and order"];
check[(select from partitioned where date=2026.11.05)~
      withPartition[2026.11.05;expected05];
      "partition filtering"];
check[(select sym,price from partitioned where date=2026.11.06)~
      (select sym,price from withPartition[2026.11.06;expected06]);
      "partition filtering with projection"];
check[(select from partitioned where date=2026.11.07)~
      withPartition[2026.11.07;expected07];
      "flat-file partition filtering"];

priceOnlyPath:`:test/fixtures/priceonly/;
priceOnly:csvdb.csvP[enlist priceOnlyPath;columnNames;typeCodes;
                      ([]date:enlist 2026.11.06)];
priceOnlyExpected:([]price:101.5 202.25);
check[(select price from priceOnly where date=2026.11.06)~
      priceOnlyExpected;
      "partition filter reads only selected columns"];

-1 "All CSVDB data tests passed.";
exit 0
