# PostgreSQL Database Management System

This directory contains the source code distribution of the PostgreSQL database management system.

PostgreSQL is an advanced object-relational database management system that supports an extended subset of the SQL standard, including transactions, foreign keys, subqueries, triggers, user-defined types and functions. This distribution also contains C language bindings.

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
{"id": 1, "name": "John Smith", "age": 32, "department": "Engineering", "salary": 85000}
{"id": 2, "name": "Sara Johnson", "age": 28, "department": "Marketing", "salary": 72000}
{"id": 3, "name": "Michael Brown", "age": 45, "department": "Finance", "salary": 110000}
{"id": 4, "name": "Patricia Davis", "age": 37, "department": "Human Resources", "salary": 65000}
{"id": 5, "name": "Robert Wilson", "age": 29, "department": "Engineering", "salary": 78000}
```

### Other COPY Options

COPY provides various options for flexible data import:

```sql
-- Importing from a CSV file
COPY employees FROM '/path/to/employees.csv' WITH (FORMAT 'csv', HEADER);

-- Importing from a text file with delimiter
COPY employees FROM '/path/to/employees.txt' WITH (DELIMITER '|');

-- Exporting data to a file
COPY employees TO '/path/to/export_file.csv' WITH (FORMAT 'csv', HEADER);
```



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
