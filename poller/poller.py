import os
import time
import requests
import psycopg2
from psycopg2.extras import execute_values

# Environment variables
ENDPOINT = os.getenv("ENDPOINT", "http://blazeguard.local/stream")
DB_HOST = os.getenv("DB_HOST", "localhost")
DB_PORT = os.getenv("DB_PORT", "5432")
DB_USER = os.getenv("DB_USER", "postgres")
DB_PASSWORD = os.getenv("DB_PASSWORD", "")
DB_NAME = os.getenv("DB_NAME", "metrics")

# Database connection
conn = psycopg2.connect(
    host=DB_HOST,
    port=DB_PORT,
    user=DB_USER,
    password=DB_PASSWORD,
    dbname=DB_NAME
)
cursor = conn.cursor()

# Ensure the target table exists
cursor.execute("""
CREATE TABLE IF NOT EXISTS telemetry (
    id SERIAL PRIMARY KEY,
    timestamp TIMESTAMPTZ DEFAULT now(),
    top_temp INTEGER,
    bottom_temp INTEGER,
    alarm_flag BOOLEAN,
    alarm_test_active BOOLEAN,
    refuel_alarm_flag BOOLEAN,
    trend TEXT,
    led_show BOOLEAN,
    refuel_level INTEGER,
    upper_limit INTEGER,
    lower_limit INTEGER,
    stove_set_point INTEGER,
    stove_set_point_active BOOLEAN,
    firmware_version TEXT,
    hardware_version TEXT,
    c_units BOOLEAN,
    alarm_ack_flag BOOLEAN,
    refuel_tones BOOLEAN,
    zip_code TEXT
);
""")
conn.commit()

def poll_endpoint():
    response = requests.get(ENDPOINT)
    response.raise_for_status()  # Raise an error for failed requests
    data = response.json()

    # Insert the JSON response into the TimescaleDB
    insert_query = """
    INSERT INTO telemetry (
        top_temp, bottom_temp, alarm_flag, alarm_test_active, refuel_alarm_flag,
        trend, led_show, refuel_level, upper_limit, lower_limit,
        stove_set_point, stove_set_point_active, firmware_version,
        hardware_version, c_units, alarm_ack_flag, refuel_tones, zip_code
    ) VALUES %s;
    """
    values = [(
        data["topTemp"],
        data["bottomTemp"],
        data["alarmFlag"],
        data["alarmTestActive"],
        data["refuelAlarmFlag"],
        data["trend"],
        data["ledshow"],
        data["refuelLevel"],
        data["upperLimit"],
        data["lowerLimit"],
        data["stoveSetPoint"],
        data["stoveSetPointActive"],
        data["firmwareVersion"],
        data["hardwareVersion"],
        data["Cunits"],
        data["alarmackflag"],
        data["RefuelTones"],
        data["zipCode"]
    )]

    execute_values(cursor, insert_query, values)
    conn.commit()


if __name__ == "__main__":
    print("Starting poller...")
    while True:
        try:
            poll_endpoint()
            print("Data inserted successfully.")
        except Exception as e:
            print(f"Error polling endpoint: {e}")
        time.sleep(10)  # Poll every 10 seconds