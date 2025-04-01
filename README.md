# Enhancement to Psqlcopy

This directory contains the source code distribution of the PostgreSQL database management system.

PostgreSQL is an advanced object-relational database management system that supports an extended subset of the SQL standard, including transactions, foreign keys, subqueries, triggers, user-defined types, and functions. This distribution also contains C language bindings.

## Implemented Features

We have enhanced the PostgreSQL `COPY` command by implementing the following features:

- **Parallel COPY**: Multi-threaded data processing for improved performance.
- **Preview Data Before Copying**: Allows users to preview data before executing the `COPY` operation.
- **Support for JSON and JSONL**: Enables importing and exporting data in JSON and JSONL formats.
- **Support for Avro**: Facilitates high-performance data exchange using the Avro format.
- **Support for Parquet**: Optimized for columnar storage, enabling efficient data retrieval.
- **Support for ORC File Format**: Adds compatibility with ORC, commonly used in big data processing.


## Installation

### Prerequisites

Before installing PostgreSQL, ensure you have the following:
- C compiler (GCC or compatible)
- Make utility
- Approximately 100 MB of disk space for the source tree during compilation
- Sufficient disk space for the installation directory
- zlib compression library

### Basic Installation Steps

1. **Download the source code**:
   ```
   git clone https://github.com/postgres/postgres.git
   cd postgres
   ```

2. **Configure the build environment**:
   ```
   ./configure
   ```
   Optional: Specify installation directory with `--prefix=/path/to/install`

3. **Build PostgreSQL**:
   ```
   make
   ```

4. **Run regression tests** (optional but recommended):
   ```
   make check
   ```

5. **Install**:
   ```
   make install
   ```

6. **Initialize the database cluster**:
   ```
   initdb -D /path/to/data/directory
   ```

7. **Start the server**:
   ```
   pg_ctl -D /path/to/data/directory -l logfile start
   ```

8. **Create your first database**:
   ```
   createdb mydb
   ```

9. **Connect to the database**:
   ```
   psql mydb
   ```

For detailed installation instructions, see: https://www.postgresql.org/docs/17/installation.html

# Implementation

### Preview Data with \copy Command

The PostgreSQL `\copy` command allows you to preview and import data from files directly from the psql client. Unlike the server-side `COPY` command, `\copy` reads from the client's file system and doesn't require server file access permissions.

```sql
-- Import CSV data with header using \copy preview
\copy preview employees FROM '/home/amalendu/college/employee-data' CSV HEADER
```
![image](https://github.com/user-attachments/assets/8c1c6514-beb4-4506-bc77-e242dc141b3c)

# Parallel COPY Implementation

This project includes a multi-threaded implementation of the PostgreSQL `COPY` command to improve data loading performance using parallel processing.

## Implemented Features

- Multi-threaded data processing for `COPY` operations
- Configurable number of worker threads (default: 8)
- Thread synchronization for protocol consistency
- Support for both text and binary formats

## Performance Comparison

We tested the implementation with a table containing **1,000,000 rows**.. Surprisingly, the parallel implementation showed slower performance than the standard single-threaded implementation :

| Implementation  | Time (ms) | Notes |
|----------------|---------------|-------|
| Standard COPY  | 1918.758          | Original PostgreSQL implementation |
| Parallel COPY  | 3386.61       | Using 8 threads |

## Analysis

The parallel implementation's slower performance can be attributed to several factors:

1. **Protocol Overhead**: PostgreSQL's `COPY` protocol requires ordered messages, which necessitates thread synchronization that adds overhead.
2. **Context Switching**: Thread creation and management overhead exceeds the benefits for this operation.
3. **I/O Bottleneck**: The data transfer is likely I/O bound rather than CPU bound.
4. **Connection Limitations**: PostgreSQL connections weren't designed for concurrent access from multiple threads.
5. When the number of thread increases the times decreases.

## Screenshot 
### parallel operation
![image](https://github.com/user-attachments/assets/d3c95297-d7d1-422a-948a-48931b0210ad)
### Single threaded execution
![image](https://github.com/user-attachments/assets/8ef4b9fe-35db-4fb2-9a8e-33df0f21e88d)

### using user input
![Screenshot From 2025-03-30 15-58-38](https://github.com/user-attachments/assets/4271d532-c46b-4a8b-9e45-10abff66fa03)

## Usage

### SQL Commands

```sql
-- Enable timing to measure performance
\timing on

-- Standard COPY operation
\copy test_copy_data FROM '/path/to/data.csv' CSV HEADER;

-- Parallel COPY operation (with 8 threads by default)
```
## Importing Data with COPY Command

PostgreSQL provides the powerful COPY command for efficiently loading data from various formats including CSV, text, and JSON files.



### Importing JSON Data

To import data from a JSON file into a PostgreSQL table:

```sql
-- First create the target table
CREATE TABLE employees (
    id INTEGER,
    name TEXT,
    age INTEGER,
    department TEXT,
    salary NUMERIC
);

-- Import JSON data using COPY command
COPY employees(id, name, age, department, salary)
FROM '/path/to/employees.json'
WITH (FORMAT 'json');
```

### Sample JSON Data Format

The JSON file should contain one complete JSON object per line (JSON Lines format). Each object's fields should match the table columns:

```json
[
    {"id":1, "name":"John Doe", "age":35, "department":"HR", "salary":60000},
    {"id":2, "name":"Jane Smith", "age":29, "department":"Finance", "salary":75000},
    {"id":3, "name":"Michael Johnson", "age":40, "department":"IT", "salary":90000},
    {"id":4, "name":"Emily Davis", "age":25, "department":"Marketing", "salary":50000},
    {"id":5, "name":"David Wilson", "age":32, "department":"Operations", "salary":68000}
]
```

### Screenshot
![image](https://github.com/user-attachments/assets/1801369a-fe83-4760-960e-243e3affb37d)




PostgreSQL now supports exporting table data directly to JSON format using the COPY command.

### Exporting to JSON

To export a table or query results to JSON format:

```sql
-- Export entire table to JSON
COPY table_name TO '/path/to/output.json' WITH (FORMAT 'json');

-- Export query results to JSON
COPY (SELECT * FROM table_name WHERE condition) TO '/path/to/output.json' WITH (FORMAT 'json');
```

The output will be a JSON array containing objects for each row:
```json
[
  {"id": 1, "name": "John Smith", "age": 30, "department": "Engineering"},
  {"id": 2, "name": "Jane Doe", "age": 28, "department": "Marketing"},
  {"id": 3, "name": "Bob Wilson", "age": 35, "department": "Finance"}
]
```

### Data Type Handling

The JSON export handles PostgreSQL data types as follows:
- Numeric types (INT, FLOAT) -> JSON numbers
- Text types (TEXT, VARCHAR) -> JSON strings
- Boolean -> JSON boolean
- NULL values -> JSON null
- Arrays -> JSON arrays
- JSON/JSONB -> Nested JSON structures
- Other types -> Converted to strings

### Example Usage

```sql
-- Create a sample table
CREATE TABLE employees (
    id INT,
    name TEXT,
    age INT,
    department TEXT,
    salary NUMERIC
);

-- Insert some data
INSERT INTO employees VALUES
    (1, 'John Smith', 30, 'Engineering', 85000),
    (2, 'Jane Doe', 28, 'Marketing', 72000);

-- Export to JSON
COPY employees TO '/tmp/employees.json' WITH (FORMAT 'json');

-- Export specific columns
COPY (SELECT name, department FROM employees) TO '/tmp/emp_names.json' WITH (FORMAT 'json');
```


### Screenshot
![image](https://github.com/user-attachments/assets/75b915bb-eee9-455a-8ef7-c916678b814c)

![image](https://github.com/user-attachments/assets/c899d221-f72f-4904-85a9-14e7fc9f636a)


## Importing and Exporting Avro Data

PostgreSQL can work with Apache Avro format through extensions and foreign data wrappers. Avro is a data serialization system that provides rich data structures, compact binary data format, and container files for storing persistent data.

### Advantages of Avro Format

1. **Schema Evolution**
   - Supports forward and backward compatibility
   - Allows adding, removing, or modifying fields without breaking existing applications
   - Schema is stored with the data, making it self-describing

2. **Compact Storage**
   - Binary format results in smaller file sizes compared to JSON or CSV
   - Efficient serialization and deserialization
   - Reduces storage costs and network bandwidth

3. **Rich Data Types**
   - Supports complex data types (arrays, maps, unions)
   - Built-in compression
   - Language-agnostic schema definition

### Working with Avro in PostgreSQL

#### Using Foreign Data Wrapper

```sql
-- Create extension if not already installed
CREATE EXTENSION avro_fdw;

-- Create foreign server
CREATE SERVER avro_server
  FOREIGN DATA WRAPPER avro_fdw
  OPTIONS (
    filename '/path/to/data.avro'
  );

-- Create foreign table
CREATE FOREIGN TABLE employees_avro (
    id INTEGER,
    name TEXT,
    age INTEGER,
    department TEXT,
    salary NUMERIC
) SERVER avro_server;
```

#### Sample Avro Schema

```json
{
  "type": "record",
  "name": "Employee",
  "fields": [
    {"name": "id", "type": "int"},
    {"name": "name", "type": "string"},
    {"name": "age", "type": "int"},
    {"name": "department", "type": "string"},
    {"name": "salary", "type": "double"}
  ]
}
```

### Importing Avro Data

```sql
-- Import data from Avro file using foreign table
INSERT INTO employees 
SELECT * FROM employees_avro;

-- Or using COPY command with extension
COPY employees FROM '/path/to/data.avro' WITH (FORMAT 'avro');
```

### Exporting to Avro

```sql
-- Export entire table to Avro format
COPY employees TO '/path/to/output.avro' WITH (FORMAT 'avro');

-- Export specific query results
COPY (
    SELECT * FROM employees 
    WHERE department = 'Engineering'
) TO '/path/to/engineers.avro' WITH (FORMAT 'avro');
```

### Screenshot
![image](https://github.com/user-attachments/assets/91ec49ab-b8e6-4fc0-a44c-75cd006b456d)
![image](https://github.com/user-attachments/assets/82740695-ccc1-4213-89a7-fba426389885)
![image](https://github.com/user-attachments/assets/69e6f17d-d205-49d1-8993-43120e5545b8)

### Performance Considerations

- Avro provides better performance for:
  - Large datasets with frequent schema changes
  - Systems requiring efficient serialization
  - Applications needing language-independent data exchange
  - Scenarios where data compression is important
 

## Running PostgreSQL

### Starting the Server

```bash
# Start the server
pg_ctl -D /path/to/data/directory start

# Start with log output
pg_ctl -D /path/to/data/directory -l logfile start
```

### Stopping the Server

```bash
# Stop the server (default: smart shutdown)
pg_ctl -D /path/to/data/directory stop

# Fast shutdown
pg_ctl -D /path/to/data/directory stop -m fast

# Immediate shutdown
pg_ctl -D /path/to/data/directory stop -m immediate
```

### Checking Server Status

```bash
pg_ctl -D /path/to/data/directory status
```

### Database Connection

```bash
# Connect to a database
psql dbname

# Connect with specific user
psql -U username dbname

# Connect to database on different host/port
psql -h hostname -p port dbname
```

## Additional Resources

- General documentation: https://www.postgresql.org/docs/17/
- Latest versions: https://www.postgresql.org/download/
- Main website: https://www.postgresql.org/
- Copyright and license information can be found in the file COPYRIGHT.
  
## Contributors
- Amalendu Manoj(https://github.com/vagabond-0)
- Krishnadev P V(https://github.com/Krishnadevpv)
- Fathima Mahim (https://github.com/fathimamahim)

