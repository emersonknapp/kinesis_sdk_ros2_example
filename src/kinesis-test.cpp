/*
Copyright 2010-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.

This file is licensed under the Apache License, Version 2.0 (the "License").
You may not use this file except in compliance with the License. A copy of
the License is located at

http://aws.amazon.com/apache2.0/

This file is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.
*/
#include <iostream>
#include <random>
#include <aws/core/Aws.h>
#include <aws/core/utils/Outcome.h>
#include <aws/kinesis/KinesisClient.h>
#include <aws/kinesis/model/DescribeStreamRequest.h>
#include <aws/kinesis/model/DescribeStreamResult.h>
#include <aws/kinesis/model/GetRecordsRequest.h>
#include <aws/kinesis/model/GetRecordsResult.h>
#include <aws/kinesis/model/GetShardIteratorRequest.h>
#include <aws/kinesis/model/GetShardIteratorResult.h>
#include <aws/kinesis/model/Shard.h>
#include <aws/kinesis/model/PutRecordsResult.h>
#include <aws/kinesis/model/PutRecordsRequest.h>
#include <aws/kinesis/model/PutRecordsRequestEntry.h>

/**
* Puts multiple records into a stream. Retrieves some records
* using a shard iterator.
*
* Takes name of a data stream to populate.
*
* This code expects that you have AWS credentials set up per:
* http://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/credentials.html
*/

void describeStream(Aws::Kinesis::KinesisClient & kinesisClient, const Aws::String & streamName)
{
    Aws::Kinesis::Model::DescribeStreamRequest describeStreamRequest;
    describeStreamRequest.SetStreamName(streamName);
    Aws::Vector<Aws::Kinesis::Model::Shard> shards;
    Aws::String exclusiveStartShardId = "";
    do
    {
        std::cout << "Asking client to describe stream" << std::endl;
        Aws::Kinesis::Model::DescribeStreamOutcome describeStreamResult = kinesisClient.DescribeStream(describeStreamRequest);
        std::cout << "Request done" << std::endl;
        Aws::Vector<Aws::Kinesis::Model::Shard> shardsTemp = describeStreamResult.GetResult().GetStreamDescription().GetShards();
        shards.insert(shards.end(), shardsTemp.begin(), shardsTemp.end());
        if (!describeStreamResult.IsSuccess()) {
            std::cout << "Describe stream result failed:" << std::endl;
            std::cout << describeStreamResult.GetError() << std::endl;
            return;
        }
        if (describeStreamResult.GetResult().GetStreamDescription().GetHasMoreShards() && shards.size() > 0)
        {
            exclusiveStartShardId = shards[shards.size() - 1].GetShardId();
            describeStreamRequest.SetExclusiveStartShardId(exclusiveStartShardId);
        }
        else
            exclusiveStartShardId = "";
    } while (exclusiveStartShardId.length() != 0);

    if (shards.size() > 0)
    {
        std::cout << "Shards found:" << std::endl;
        for (auto shard : shards)
        {
            std::cout << shard.GetShardId() << std::endl;
        }

        Aws::Kinesis::Model::GetShardIteratorRequest getShardIteratorRequest;
        getShardIteratorRequest.SetStreamName(streamName);
        // use the first shard found
        getShardIteratorRequest.SetShardId(shards[0].GetShardId());
        getShardIteratorRequest.SetShardIteratorType(Aws::Kinesis::Model::ShardIteratorType::TRIM_HORIZON);

        Aws::Kinesis::Model::GetShardIteratorOutcome getShardIteratorResult = kinesisClient.GetShardIterator(getShardIteratorRequest);
        Aws::String shardIterator = getShardIteratorResult.GetResult().GetShardIterator();

        Aws::Kinesis::Model::GetRecordsRequest getRecordsRequest;
        getRecordsRequest.SetShardIterator(shardIterator);
        getRecordsRequest.SetLimit(25);

        // pull down 100 records
        std::cout << "Retrieving 100 records" << std::endl;
        for (int i = 0; i < 4; i++)
        {
            Aws::Kinesis::Model::GetRecordsOutcome getRecordsResult = kinesisClient.GetRecords(getRecordsRequest);
            for (auto r : getRecordsResult.GetResult().GetRecords())
            {
                Aws::String s((char*)r.GetData().GetUnderlyingData());
                std::cout << s.substr(0, r.GetData().GetLength()) << std::endl;
            }
            shardIterator = getRecordsResult.GetResult().GetNextShardIterator();
        }
    } else {
        std::cout << "NO SHARDS??" << std::endl;
    }
}

void putSomeRecords(Aws::Kinesis::KinesisClient & kinesisClient, const Aws::String & streamName)
{
    std::random_device rd;
    std::mt19937 mt_rand(rd());
    Aws::Vector<Aws::String> animals{"dog", "cat", "mouse", "horse", "stoat", "snake"};
    Aws::Kinesis::Model::PutRecordsRequest putRecordsRequest;
    putRecordsRequest.SetStreamName(streamName);
    Aws::Vector<Aws::Kinesis::Model::PutRecordsRequestEntry> putRecordsRequestEntryList;

    // create 500 records
    std::cout << "Adding records to stream \"" << streamName << "\"" << std::endl;
    for (int i = 0; i < 500; i++)
    {
        Aws::Kinesis::Model::PutRecordsRequestEntry putRecordsRequestEntry;
        Aws::StringStream pk;
        pk << "pk-" << (i % 100);
        putRecordsRequestEntry.SetPartitionKey(pk.str());
        Aws::StringStream data;
        data << i << ", " << animals[mt_rand() % animals.size()] << ", " << mt_rand() << ", " << mt_rand() * (float).001;
        Aws::Utils::ByteBuffer bytes((unsigned char*)data.str().c_str(), data.str().length());
        putRecordsRequestEntry.SetData(bytes);
        putRecordsRequestEntryList.emplace_back(putRecordsRequestEntry);
    }
    putRecordsRequest.SetRecords(putRecordsRequestEntryList);
    std::cout << "Created records - putting to kinesis client" << std::endl;
    Aws::Kinesis::Model::PutRecordsOutcome putRecordsResult = kinesisClient.PutRecords(putRecordsRequest);
    if (!putRecordsResult.IsSuccess()) {
        std::cout << "IT DIDNT WORK" << std::endl;
        std::cout << putRecordsResult.GetError() << std::endl;
        return;
    }
    std::cout << "Done" << std::endl;

    // if one or more records were not put, retry them
    while (putRecordsResult.GetResult().GetFailedRecordCount() > 0)
    {
        std::cout << "Some records failed, retrying" << std::endl;
        Aws::Vector<Aws::Kinesis::Model::PutRecordsRequestEntry> failedRecordsList;
        Aws::Vector<Aws::Kinesis::Model::PutRecordsResultEntry> putRecordsResultEntryList = putRecordsResult.GetResult().GetRecords();
        for (unsigned int i = 0; i < putRecordsResultEntryList.size(); i++)
        {
            Aws::Kinesis::Model::PutRecordsRequestEntry putRecordRequestEntry = putRecordsRequestEntryList[i];
            Aws::Kinesis::Model::PutRecordsResultEntry putRecordsResultEntry = putRecordsResultEntryList[i];
            if (putRecordsResultEntry.GetErrorCode().length() > 0)
                failedRecordsList.emplace_back(putRecordRequestEntry);
        }
        putRecordsRequestEntryList = failedRecordsList;
        putRecordsRequest.SetRecords(putRecordsRequestEntryList);
        putRecordsResult = kinesisClient.PutRecords(putRecordsRequest);
    }
}


int main(int argc, char** argv)
{
    const std::string USAGE = "\n" \
        "Usage:\n"
        "    put_get_records <streamname>\n\n"
        "Where:\n"
        "    streamname - the table to delete the item from.\n\n"
        "Example:\n"
        "    put_get_records sample-stream\n\n";

    if (argc != 2)
    {
        std::cout << USAGE;
        return 1;
    }

    Aws::SDKOptions options;
    options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Trace;
    Aws::InitAPI(options);
    do {
        const Aws::String streamName(argv[1]);


        Aws::Client::ClientConfiguration config;
        config.scheme = Aws::Http::Scheme::HTTPS;
        config.connectTimeoutMs = 2000;
        config.requestTimeoutMs = 2000;
        config.region = Aws::Region::US_WEST_2;
        config.verifySSL = true;

        // config.verifySSL = true;
        std::cout << "Creating client" << std::endl;
        Aws::Kinesis::KinesisClient kinesisClient(config);

        // describeStream(kinesisClient, streamName);
        putSomeRecords(kinesisClient, streamName);

    } while (0);
    Aws::ShutdownAPI(options);
    return 0;
}
