import socket
import argparse
import sys
import time

def read_line(sock, timeout=2.0):
    sock.settimeout(timeout)
    line = b""
    while True:
        try:
            char = sock.recv(1)
            if not char:
                break
            line += char
            if char == b"\n":
                break
        except socket.timeout:
            break
    return line.decode("utf-8").strip()

def send_cmd(sock, cmd, expect_lines=1):
    print(f"-> {cmd}")
    sock.sendall((cmd + "\n").encode("utf-8"))
    responses = []
    
    # Per comandi come LIST UPS o LIST VAR, leggiamo fino al corrispondente END
    cmd_upper = cmd.upper()
    if cmd_upper.startswith("LIST UPS"):
        while True:
            line = read_line(sock)
            if not line:
                break
            responses.append(line)
            print(f"<- {line}")
            if "END LIST UPS" in line:
                break
    elif cmd_upper.startswith("LIST VAR"):
        while True:
            line = read_line(sock)
            if not line:
                break
            responses.append(line)
            print(f"<- {line}")
            if "END LIST VAR" in line:
                break
    else:
        for _ in range(expect_lines):
            line = read_line(sock)
            responses.append(line)
            print(f"<- {line}")
            
    return responses

def run_test():
    parser = argparse.ArgumentParser(description="Home Assistant NUT Client Emulator")
    parser.add_argument("--host", default="127.0.0.1", help="Indirizzo IP del server NUT")
    parser.add_argument("--port", type=int, default=3493, help="Porta TCP del server NUT")
    parser.add_argument("--user", default="admin", help="Username NUT")
    parser.add_argument("--password", default="nut_password", help="Password NUT")
    parser.add_argument("--ups", default="eaton", help="Nome dell'UPS")
    
    args = parser.parse_args()
    
    print(f"Connessione a {args.host}:{args.port}...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((args.host, args.port))
    except Exception as e:
        print(f"Errore di connessione: {e}")
        sys.exit(1)
        
    print("Connessione stabilita. Avvio test...")
    
    # 1. Test Autenticazione (se configurata)
    resp = send_cmd(sock, f"USERNAME {args.user}")
    assert resp[0] == "OK", f"Fallito USERNAME: {resp}"
    
    resp = send_cmd(sock, f"PASSWORD {args.password}")
    assert resp[0] == "OK", f"Fallito PASSWORD: {resp}"
    
    # 2. Test LIST UPS
    resp = send_cmd(sock, "LIST UPS")
    assert len(resp) >= 3, "Risposta LIST UPS troppo corta"
    assert resp[0] == "BEGIN LIST UPS", "LIST UPS deve iniziare con BEGIN"
    assert any(args.ups in line for line in resp), f"UPS '{args.ups}' non trovato nella lista"
    assert resp[-1] == "END LIST UPS", "LIST UPS deve terminare con END"
    
    # 3. Test LIST VAR
    resp = send_cmd(sock, f"LIST VAR {args.ups}")
    assert len(resp) >= 5, "Risposta LIST VAR troppo corta"
    assert resp[0] == f"BEGIN LIST VAR {args.ups}", f"LIST VAR deve iniziare con BEGIN per {args.ups}"
    
    # Estraiamo le variabili
    vars_dict = {}
    for line in resp[1:-1]:
        tokens = line.split(" ")
        if len(tokens) >= 4 and tokens[0] == "VAR":
            var_name = tokens[2]
            var_value = tokens[3].replace('"', '')
            vars_dict[var_name] = var_value
            
    print(f"Variabili rilevate: {vars_dict}")
    assert "battery.charge" in vars_dict, "Variabile battery.charge mancante"
    assert "input.voltage" in vars_dict, "Variabile input.voltage mancante"
    assert "ups.status" in vars_dict, "Variabile ups.status mancante"
    
    # 4. Test GET VAR per ciascuna variabile
    for var in ["battery.charge", "input.voltage", "ups.status"]:
        resp = send_cmd(sock, f"GET VAR {args.ups} {var}")
        assert len(resp) == 1, f"Risposta GET VAR {var} non valida"
        assert resp[0].startswith(f"VAR {args.ups} {var}"), f"Intestazione risposta GET VAR errata: {resp[0]}"
        val = resp[0].split(" ")[-1].replace('"', '')
        assert val == vars_dict[var], f"Il valore da GET VAR ({val}) non coincide con LIST VAR ({vars_dict[var]})"
        
    # 5. Test Caso-Insensitivity per comandi
    resp = send_cmd(sock, f"get var {args.ups.upper()} BATTERY.CHARGE")
    assert len(resp) == 1 and resp[0].startswith(f"VAR"), f"Fallito test Case-Insensitivity: {resp}"
    
    # 6. Test Errori
    resp = send_cmd(sock, "GET VAR non_existent battery.charge")
    assert resp[0] == "ERR UNKNOWN-UPS", f"Atteso ERR UNKNOWN-UPS, ottenuto: {resp}"
    
    resp = send_cmd(sock, f"GET VAR {args.ups} non.existent")
    assert resp[0] == "ERR VAR-NOT-SUPPORTED", f"Atteso ERR VAR-NOT-SUPPORTED, ottenuto: {resp}"
    
    resp = send_cmd(sock, "GET VAR")
    assert resp[0] == "ERR INVALID-ARGUMENT", f"Atteso ERR INVALID-ARGUMENT, ottenuto: {resp}"
    
    # 7. Test Logout
    resp = send_cmd(sock, "LOGOUT")
    assert resp[0] == "OK Goodbye", f"Fallito LOGOUT: {resp}"
    
    sock.close()
    print("\n🎉 Tutti i test di conformità a Home Assistant sono passati con successo!")

if __name__ == "__main__":
    run_test()
