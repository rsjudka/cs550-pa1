from collections import defaultdict


def get_avg(files):
    request_deltas = list()
    for file in files:
        requests = defaultdict(dict)
        with open(file, 'r') as f:
            for line in f.readlines():
                if line[0] == '!' and 'search request' in line:
                    data = line.split()
                    requests[data[0][1]].update({data[-1][1:-1]:data[1][1:-1]})
        for request in requests.values():
            request_deltas.append(int(request['end']) - int(request['start']))
    return sum(request_deltas) / len(request_deltas)


one_client_avg = get_avg(['one_client/55001_client.log'])

two_clients_avg = get_avg(["two_clients/55{:03d}_client.log".format(i) for i in range(1, 3)])

three_clients_avg = get_avg(["three_clients/55{:03d}_client.log".format(i) for i in range(1, 4)])

four_clients_avg = get_avg(["four_clients/55{:03d}_client.log".format(i) for i in range(1, 5)])

five_clients_avg = get_avg(["five_clients/55{:03d}_client.log".format(i) for i in range(1, 6)])

six_clients_avg = get_avg(["six_clients/55{:03d}_client.log".format(i) for i in range(1, 7)])

seven_clients_avg = get_avg(["seven_clients/55{:03d}_client.log".format(i) for i in range(1, 8)])

eight_clients_avg = get_avg(["eight_clients/55{:03d}_client.log".format(i) for i in range(1, 9)])

nine_clients_avg = get_avg(["nine_clients/55{:03d}_client.log".format(i) for i in range(1, 10)])

ten_clients_avg = get_avg(["ten_clients/55{:03d}_client.log".format(i) for i in range(1, 11)])


print one_client_avg, two_clients_avg, three_clients_avg, four_clients_avg, five_clients_avg, six_clients_avg, seven_clients_avg, eight_clients_avg, nine_clients_avg, ten_clients_avg