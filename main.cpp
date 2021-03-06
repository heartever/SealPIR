#include "pir.hpp"
#include "pir_client.hpp"
#include "pir_server.hpp"
#include <chrono>
#include <random>

using namespace chrono;
using namespace seal;

int main(int argc, char *argv[]) {

    // uint64_t number_of_items = 1 << 13;
    // uint64_t number_of_items = 4096;
     uint64_t number_of_items = 1 << 16;

    uint64_t size_per_item = 288; // in bytes
    // uint64_t size_per_item = 1 << 10; // 1 KB.
    // uint64_t size_per_item = 10 << 10; // 10 KB.

    uint32_t N = 2048;
    uint32_t logt = 20;
    uint32_t d = 2;

    EncryptionParameters params;
    EncryptionParameters expanded_params;
    PirParams pir_params;

    // Generates all parameters
    cout << "Generating all parameters" << endl;
    gen_params(number_of_items, size_per_item, N, logt, d, params, expanded_params, pir_params);

    // Create test database
    uint8_t *db = (uint8_t *)malloc(number_of_items * size_per_item);

    random_device rd;
    for (uint64_t i = 0; i < number_of_items; i++) {
        for (uint64_t j = 0; j < size_per_item; j++) {
            *(db + (i * size_per_item) + j) = rd() % 256;
        }
    }

    // Initialize PIR Server
    cout << "Initializing server and client" << endl;
    PIRServer server(expanded_params, pir_params);

    // Initialize PIR client....
    PIRClient client(params, expanded_params, pir_params);
    GaloisKeys galois_keys = client.generate_galois_keys();

    // Set galois key
    cout << "Setting Galois keys" << endl;
    server.set_galois_key(0, galois_keys);


    // The following can be used to update parameters rather than creating new instances
    // (here it doesn't do anything).
    cout << "Updating database size to: " << number_of_items << " elements" << endl;
    update_params(number_of_items, size_per_item, d, params, expanded_params, pir_params);

    uint32_t logtp = ceil(log2(expanded_params.plain_modulus().value()));

    cout << "logtp: " << logtp << endl;

    client.update_parameters(expanded_params, pir_params);
    server.update_parameters(expanded_params, pir_params);

    // Measure database setup
    auto time_pre_s = high_resolution_clock::now();
    server.set_database(db, number_of_items, size_per_item);
    server.preprocess_database();
    auto time_pre_e = high_resolution_clock::now();
    auto time_pre_us = duration_cast<microseconds>(time_pre_e - time_pre_s).count();

    // Choose an index of an element in the DB
    uint64_t ele_index = rd() % number_of_items; // element in DB at random position
    uint64_t index = client.get_fv_index(ele_index, size_per_item);   // index of FV plaintext
    uint64_t offset = client.get_fv_offset(ele_index, size_per_item); // offset in FV plaintext

    // Measure query generation
    auto time_query_s = high_resolution_clock::now();
    PirQuery query = client.generate_query(index);
    auto time_query_e = high_resolution_clock::now();
    auto time_query_us = duration_cast<microseconds>(time_query_e - time_query_s).count();

    // Measure query processing (including expansion)
    auto time_server_s = high_resolution_clock::now();
    PirQuery query_ser = deserialize_ciphertexts(d, serialize_ciphertexts(query), CIPHER_SIZE);
    PirReply reply = server.generate_reply(query_ser, 0);
    auto time_server_e = high_resolution_clock::now();
    auto time_server_us = duration_cast<microseconds>(time_server_e - time_server_s).count();

    // Measure response extraction
    auto time_decode_s = chrono::high_resolution_clock::now();
    Plaintext result = client.decode_reply(reply);
    auto time_decode_e = chrono::high_resolution_clock::now();
    auto time_decode_us = duration_cast<microseconds>(time_decode_e - time_decode_s).count();

    // Convert to elements
    vector<uint8_t> elems(N * logtp / 8);
    coeffs_to_bytes(logtp, result, elems.data(), (N * logtp) / 8);

    // Check that we retrieved the correct element
    for (uint32_t i = 0; i < size_per_item; i++) {
        if (elems[(offset * size_per_item) + i] != db[(ele_index * size_per_item) + i]) {
            cout << "elems " << (int)elems[(offset * size_per_item) + i] << ", db "
                 << (int)db[(ele_index * size_per_item) + i] << endl;
            cout << "PIR result wrong!" << endl;
            return -1;
        }
    }

    // Output results
    cout << "PIRServer pre-processing time: " << time_pre_us / 1000 << " ms" << endl;
    cout << "PIRServer query processing generation time: " << time_server_us / 1000 << " ms"
         << endl;
    cout << "PIRClient query generation time: " << time_query_us / 1000 << " ms" << endl;
    cout << "PIRClient answer decode time: " << time_decode_us / 1000 << " ms" << endl;
    cout << "Reply num ciphertexts: " << reply.size() << endl;

    return 0;
}
