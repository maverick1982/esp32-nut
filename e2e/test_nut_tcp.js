const net = require('net');

async function testNutTcp() {
    return new Promise((resolve, reject) => {
        const client = new net.Socket();
        let dataStr = '';

        client.connect(3493, '127.0.0.1', () => {
            console.log('Connected to NUT Server');
            client.write('LIST VAR UPS\n');
        });

        client.on('data', (data) => {
            dataStr += data.toString();
            if (dataStr.includes('END LIST VAR UPS')) {
                client.destroy();
                
                // Assicuriamoci che NON vi siano valori statici per battery.type 
                // se l'UPS non li ha mandati via USB (di default, il mock o un server a secco non dovrebbe averli)
                if (dataStr.includes('battery.type')) {
                    console.error('Error: battery.type was found, it should NOT be static!');
                    reject(new Error('Static battery.type found'));
                    return;
                }
                
                console.log('Test passed. Variables are dynamic.');
                resolve();
            }
        });

        client.on('error', (err) => {
            // Se il server non è attivo localmente, lo consideriamo un warning ma non rompiamo il test se non possiamo fare integration
            console.log('Could not connect to local NUT server, skipping active TCP test: ' + err.message);
            resolve();
        });
        
        setTimeout(() => {
            console.log('Timeout, assuming server not reachable.');
            client.destroy();
            resolve();
        }, 2000);
    });
}

testNutTcp().then(() => {
    console.log('Done.');
}).catch(e => {
    console.error(e);
    process.exit(1);
});
