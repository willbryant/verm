require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))

class ReadReplicationTest < Verm::TestCase
  def port_for(instance_n)
    VERM_SPAWNER.port + 2 + instance_n
  end

  def replicas(range)
    range.collect do |n|
      replicate_to = range.collect {|rn| "#{VERM_SPAWNER.hostname}:#{port_for(rn)}" unless rn == n}.compact
      VermSpawner.new(VERM_SPAWNER.verm_binary, "#{VERM_SPAWNER.verm_data}_replica#{n}", :port => port_for(n), :replicate_to => replicate_to)
    end
  end

  def test_reads_missing_files_with_extensions_from_other_replicas
    spawners = replicas(0..2)
    spawners.each(&:setup)

    copy_arbitrary_file_to('somefiles', 'jpg', spawner: spawners[1])

    # even though the file is only present on one replica, it should now be readable through any
    get_options = {
      :expected_content => File.read(fixture_file_path('binary_file'), :mode => 'rb'),
      :expected_content_type => "image/jpeg",
      :expected_content_encoding => nil,
      :path => @location,
    }

    assert_statistics_changes spawners, [
      {:get_requests => 1, :get_requests_found_on_replica => 1},
      {:get_requests => 1},
      {:get_requests => 1, :get_requests_not_found => 1},
    ] do
      get get_options.merge(:verm => spawners[0])
    end

    assert_statistics_changes spawners, [
      {},
      {:get_requests => 1},
      {},
    ] do
      get get_options.merge(:verm => spawners[1])
    end

    assert_statistics_changes spawners, [
      {:get_requests => 1, :get_requests_not_found => 1},
      {:get_requests => 1},
      {:get_requests => 1, :get_requests_found_on_replica => 1},
    ] do
      get get_options.merge(:verm => spawners[2])
    end
  ensure
    spawners.each(&:teardown) if spawners
  end

  def test_reads_missing_files_without_extensions_from_other_replicas
    spawners = replicas(0..1)
    spawners.each(&:setup)

    copy_arbitrary_file_to('somefiles', nil, spawner: spawners[1])

    # even though the file is only present on one replica, it should now be readable through any
    get_options = {
      :expected_content => File.read(fixture_file_path('binary_file'), :mode => 'rb'),
      :expected_content_type => "application/octet-stream",
      :expected_content_encoding => nil,
      :path => @location,
    }

    assert_statistics_changes spawners, [
      {:get_requests => 1, :get_requests_found_on_replica => 1},
      {:get_requests => 1},
    ] do
      get get_options.merge(:verm => spawners[0])
    end

    assert_statistics_changes spawners, [
      {},
      {:get_requests => 1},
    ] do
      get get_options.merge(:verm => spawners[1])
    end
  ensure
    spawners.each(&:teardown) if spawners
  end

  def test_reads_compressed_missing_files_from_other_replicas
    spawners = replicas(0..1)
    spawners.each(&:setup)

    copy_arbitrary_file_to('somefiles', 'jpg', compressed: true, spawner: spawners[1])

    # even though the file is only present on one replica, it should now be readable through any
    get_options = {
      :expected_content => File.read(fixture_file_path('binary_file.gz'), :mode => 'rb'),
      :expected_content_type => "image/jpeg",
      :expected_content_encoding => 'gzip',
      :path => @location,
    }

    assert_statistics_changes spawners, [
      {:get_requests => 1, :get_requests_found_on_replica => 1},
      {:get_requests => 1},
    ] do
      get get_options.merge(:verm => spawners[0])
    end

    assert_statistics_changes spawners, [
      {},
      {:get_requests => 1},
    ] do
      get get_options.merge(:verm => spawners[1])
    end
  ensure
    spawners.each(&:teardown) if spawners
  end

  def test_no_need_to_forward_requests_to_invalid_paths
    spawners = replicas(0..1)
    spawners.each(&:setup)

    filename = 'IF/P8unS2JIuR6_UZI5pZ0lxWHhfvR2ocOcRAma_lEiA'

    invalid_paths = [
      "/foo/#{filename}x",
      "/foo/#{filename[0..-2]}",
      "/foo/-#{filename[1..-1]}",

      "/foo/#{filename}x.jpg",
      "/foo/#{filename[0..-2]}.jpg",
      "/foo/-#{filename[1..-1]}.jpg",
    ]

    assert_statistics_changes spawners, [
      {:get_requests => invalid_paths.size, :get_requests_not_found => invalid_paths.size},
      {},
    ] do
      invalid_paths.each do |path|
        get :path => path, :expected_response_code => 404, :verm => spawners[0]
      end
    end
  ensure
    spawners.each(&:teardown) if spawners
  end
end
