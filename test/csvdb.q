// Run from the project root with:
// QPATH="$PWD:$HOME/.kx/mod" /home/brian/.kx/bin/q test/csvdb.q -q

csvdb:use`csvdb;

columnNames:`sym`ex`price`size`time;
typeCodes:"ssfjt";
typeMap:columnNames!upper typeCodes;
dates:2026.11.05 2026.11.06;
dirs:`:data/2026.11.05/t/`:data/2026.11.06/t/;

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

partitionKeys:([]date:dates);
partitioned:csvdb.csvP[dirs;columnNames;typeCodes;partitionKeys];
expectedPartitions:withPartition[2026.11.05;expected05],
                   withPartition[2026.11.06;expected06];
actualPartitions:select from partitioned;

check[(meta partitioned)~(meta expectedPartitions);
      "partitioned metadata"];
check[(count actualPartitions)=20000;"combined partition row count"];
check[actualPartitions~expectedPartitions;"combined partition values and order"];
check[(select from partitioned where date=2026.11.05)~
      withPartition[2026.11.05;expected05];
      "partition filtering"];
check[(select sym,price from partitioned where date=2026.11.06)~
      (select sym,price from withPartition[2026.11.06;expected06]);
      "partition filtering with projection"];

-1 "All CSVDB data tests passed.";
exit 0
